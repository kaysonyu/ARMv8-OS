#include <kernel/proc.h>
#include <aarch64/mmu.h>
#include <fs/block_device.h>
#include <fs/cache.h> 
#include <kernel/paging.h>
#include <common/defines.h>
#include <kernel/pt.h>
#include <common/sem.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/init.h>
#include <kernel/proc.h>
#include <kernel/cpu.h>

define_rest_init(paging){
	//TODO init		
}

u64 sbrk(i64 size){
	//TODO
	struct section* heap_section = get_heap(&thisproc()->pgdir);
	u64 origin_end = heap_section->end;
	if (size >= 0) {
		heap_section->end += size*PAGE_SIZE;
	}
	else {
		ASSERT(heap_section->end + size*PAGE_SIZE >= heap_section->begin);
		heap_section->end += size*PAGE_SIZE;
		// printk("-size:%lld\n", heap_section->end);
		if (heap_section->flags & ST_SWAP) {
			for (u64 va = heap_section->end; va < heap_section->end-size*PAGE_SIZE; va+=PAGE_SIZE) {
				PTEntriesPtr pte = get_pte(&thisproc()->pgdir, va, false);
				if (pte == NULL) continue;
				if (*pte == 0) continue;

				if (!(*pte & PTE_VALID)) {
					release_8_blocks(*pte >> 12);
				}
				*pte = 0;
			}
		}
		else {
			for (u64 va = heap_section->end; va < heap_section->end-size*PAGE_SIZE; va+=PAGE_SIZE) {
				PTEntriesPtr pte = get_pte(&thisproc()->pgdir, va, false);
				if (pte == NULL) continue;
				if (*pte == 0) continue;
				
				if (*pte & PTE_VALID) {
					u64* ka = (u64*)P2K(*pte & (~(u64)PTE_MASK));
					kfree_page(ka);
				}
				*pte = 0;
			}
		}
		
	}
	arch_tlbi_vmalle1is();

	return origin_end;
}	

static void create_section(ListNode* section_head, u64 flags) {
	struct section* section = kalloc(sizeof(struct section));
	section->flags = flags;
	section->begin = 0;
	section->end = 0;
	init_sleeplock(&section->sleeplock);
	_insert_into_list(section_head, &section->stnode);
}

void init_sections(ListNode* section_head) {
	create_section(section_head, ST_TEXT);
	create_section(section_head, ST_DATA);
}

void free_sections(struct pgdir* pd) {
	while (!_empty_list(&pd->section_head)) {
        ListNode* section_node = pd->section_head.next; 
        _detach_from_list(section_node);
        
        struct section* section = container_of(section_node, struct section, stnode); 
		if (section->flags & ST_SWAP) {
			for (u64 va = section->begin; va < section->end; va += PAGE_SIZE) {
				PTEntriesPtr pte = get_pte(pd, va, false);
				if (pte == NULL) continue;
				if (*pte == 0) continue;

				if (!(*pte & PTE_VALID)) {
					release_8_blocks(*pte >> 12);
				}
			}
		}
		else {
			for (u64 va = section->begin; va < section->end; va += PAGE_SIZE) {
				PTEntriesPtr pte = get_pte(pd, va, false);
				if (pte == NULL) continue;
				if (*pte == 0) continue;
				
				if (*pte & PTE_VALID) {
					u64* ka = (u64*)P2K(*pte & (~(u64)PTE_MASK));
					kfree_page(ka);
				}
			}
		}
		kfree(section);
    }
}

struct section* get_heap(struct pgdir* pd) {
	struct section* heap_section = NULL;
	_for_in_list(section_node, &pd->section_head) {
		if (section_node == &pd->section_head)	continue;
		struct section* section = container_of(section_node, struct section, stnode); 
		if (section->flags & ST_HEAP) {
			heap_section = section;
			break;
		}
	}
	ASSERT(heap_section);
	return heap_section;
}


void* alloc_page_for_user(){
	while (left_page_cnt() <= REVERSED_PAGES){ //this is a soft limit
		//TODO
		struct proc* swap_proc = get_offline_proc();
		struct section* heap_section = get_heap(&swap_proc->pgdir);
		if (swap_proc->pgdir.online || (heap_section->flags & ST_SWAP)) {
			_release_spinlock(&swap_proc->pgdir.lock);
			break;
		}
		else {
			swapout(&swap_proc->pgdir, heap_section);
			break;
		}
	}
	return kalloc_page();
}

//caller must have the pd->lock
void swapout(struct pgdir* pd, struct section* st){
	ASSERT(!(st->flags & ST_SWAP));
	st->flags |= ST_SWAP;
	//TODO
	u64 begin, end;
	for (u64 va = st->begin; va < st->end; va += PAGE_SIZE) {
		PTEntriesPtr pte = get_pte(pd, va, false);
		if (pte != NULL) {
			*pte &= ~((u64)PTE_VALID);
		}
	}
	begin = st->begin;
	end = st->end;
	unalertable_wait_sem(&st->sleeplock);
	_release_spinlock(&pd->lock);
	if (!(st->flags & ST_FILE)) {
		// printk("start:%lld, end:%lld\n", begin, end);
		for (u64 va = begin; va < end; va += PAGE_SIZE) {
			// printk("va:%llx, va_: %lld\n", va, *(i64*)va);
			PTEntriesPtr pte = get_pte(pd, va, false);
			if (pte == NULL) continue;
			if (*pte == 0) continue;

			u64* ka = (u64*)P2K(*pte & (~(u64)PTE_MASK));
			// printk("ka:%p\n", ka);
			u32 bno = write_page_to_disk(ka);
			*pte = (*pte & PTE_MASK) | ((u64)bno << 12);
			kfree_page(ka);
		}
	}

	_post_sem(&st->sleeplock);

}

//Free 8 continuous disk blocks
void swapin(struct pgdir* pd, struct section* st){
	ASSERT(st->flags & ST_SWAP);
	//TODO
	unalertable_wait_sem(&st->sleeplock);
	
	for (u64 va = st->begin; va < st->end; va += PAGE_SIZE) {
		PTEntriesPtr pte = get_pte(pd, va, false);
		if (pte == NULL) continue;
		if (*pte == 0) continue;
		
		void* ka = alloc_page_for_user();
		read_page_from_disk(ka, *pte >> 12);
		vmmap(pd, va, ka, PTE_USER_DATA);
	}

	st->flags &= ~(u64)ST_SWAP;

	_post_sem(&st->sleeplock);
}

int pgfault(u64 iss){
	// printk("ISS: %llx\n", iss);
	struct proc* p = thisproc();
	struct pgdir* pd = &p->pgdir;
	u64 addr = arch_get_far();
	// printk("addr: %llx\n", addr);
	//TODO

	struct section* find_section = get_heap(pd);
	PTEntriesPtr pte = get_pte(pd, addr, true);

	if (*pte == 0) {
		if (find_section->flags & ST_SWAP) {
			swapin(pd, find_section);
		}
		void* ka = alloc_page_for_user();
		vmmap(pd, addr, ka, PTE_USER_DATA);
	}
	else if (!(*pte & PTE_VALID) && (find_section->flags & ST_SWAP)) {
		swapin(pd, find_section);
	}
	else if ((*pte & PTE_VALID) && (*pte & PTE_RO)) {
		void* ka_old = (void*)P2K(*pte & ~(u64)PTE_MASK);
		
		void* ka = alloc_page_for_user();
		memcpy(ka, ka_old, PAGE_SIZE);
		vmmap(pd, addr, ka, PTE_USER_DATA);
		
		kfree_page(ka_old);
	}

	//无意义
	ASSERT(true|iss);


	arch_tlbi_vmalle1is();
	return 0;
}
