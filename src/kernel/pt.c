#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>
#include <kernel/paging.h>

PTEntriesPtr get_pte(struct pgdir* pgdir, u64 va, bool alloc)
{
    // TODO
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.

    PTEntriesPtr pt0, pt1, pt2, pt3;

    pt0 = pgdir -> pt;
    if (pt0 == NULL && !alloc) return NULL;
    if (pt0 == NULL) {
        pt0 = kalloc_page();
        memset(pt0, 0, PAGE_SIZE); 
        pgdir -> pt = pt0;
    }

    pt1 = (PTEntriesPtr)(P2K(PTE_ADDRESS(pt0[VA_PART0(va)])));
    if (!IS_VALID(pt0[VA_PART0(va)]) && !alloc) return NULL;
    if (!IS_VALID(pt0[VA_PART0(va)])) {
        pt1 = kalloc_page();
        memset(pt1, 0, PAGE_SIZE); 
        pt0[VA_PART0(va)] = K2P(pt1) | PTE_TABLE;
    }

    pt2 = (PTEntriesPtr)(P2K(PTE_ADDRESS(pt1[VA_PART1(va)])));
    if (!IS_VALID(pt1[VA_PART1(va)]) && !alloc) return NULL;
    if (!IS_VALID(pt1[VA_PART1(va)])) {
        pt2 = kalloc_page();
        memset(pt2, 0, PAGE_SIZE); 
        pt1[VA_PART1(va)] = K2P(pt2) | PTE_TABLE;
    }

    pt3 = (PTEntriesPtr)(P2K(PTE_ADDRESS(pt2[VA_PART2(va)])));
    if (!IS_VALID(pt2[VA_PART2(va)]) && !alloc) return NULL;
    if (!IS_VALID(pt2[VA_PART2(va)])) {
        pt3 = kalloc_page();
        memset(pt3, 0, PAGE_SIZE); 
        pt2[VA_PART2(va)] = K2P(pt3) | PTE_TABLE;
    }

    return &pt3[VA_PART3(va)];
}

void init_pgdir(struct pgdir* pgdir)
{
    pgdir->pt = kalloc_page();
    memset(pgdir->pt, 0, PAGE_SIZE); 
    init_spinlock(&pgdir->lock);
    init_list_node(&pgdir->section_head);
    //create_file_sections(&pgdir->section_head);
    pgdir->online = false;
}

void copy_pgdir(struct pgdir* from_pgdir, struct pgdir* to_pgdir) {
    _for_in_list(node, &from_pgdir->section_head) {
        if (node == &from_pgdir->section_head)  continue;

        struct section* from_section = container_of(node, struct section, stnode);

        struct section* to_section = kalloc(sizeof(struct section));
        to_section->begin = from_section->begin;
		to_section->end = from_section->end;
		to_section->flags = from_section->flags;
        init_sleeplock(&to_section->sleeplock);
        _insert_into_list(&to_pgdir->section_head, &to_section->stnode);

        for (u64 va = from_section->begin; va < from_section->end; va += PAGE_SIZE) {
            PTEntriesPtr pte = get_pte(from_pgdir, va, false);
            u64 flags = PTE_FLAGS(*pte);
			u64 from_ka = P2K(PTE_ADDRESS(*pte));

            void* ka = alloc_page_for_user();
            memmove(ka, (void*)from_ka, PAGE_SIZE);

            vmmap(to_pgdir, va, ka, flags);
        }
    }
}

void traverse_free(PTEntriesPtr table, u32 traverse_n) {
    if (--traverse_n) {
        for (u32 i = 0; i < N_PTE_PER_TABLE; i++) {
            if (table[i]) {
                PTEntriesPtr p = (PTEntriesPtr)(P2K(PTE_ADDRESS(table[i])));
                traverse_free(p, traverse_n);
            }
        }
    }  
    kfree_page(table);
}

void free_pgdir(struct pgdir* pgdir)
{
    // TODO
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    free_sections(pgdir);
    PTEntriesPtr pt0 = pgdir -> pt;
    if (pt0 != NULL) {
        traverse_free(pt0, 4);
    }

}

void attach_pgdir(struct pgdir* pgdir)
{
    extern PTEntries invalid_pt;
    _acquire_spinlock(&thisproc()->pgdir.lock);
    thisproc()->pgdir.online = false;
    _release_spinlock(&thisproc()->pgdir.lock);
    
    if (pgdir->pt) 
        arch_set_ttbr0(K2P(pgdir->pt));
    
    else
        arch_set_ttbr0(K2P(&invalid_pt));
    
    _acquire_spinlock(&pgdir->lock);
    pgdir->online = true;
    _release_spinlock(&pgdir->lock);
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir* pd, void* va, void *p, usize len){
    u64 va_base, ka_base, off, n;

    while (len > 0) {
        va_base = PAGE_BASE((u64)va);
        ka_base = P2K(PTE_ADDRESS(*get_pte(pd, va_base, false)));
        off = (u64)va - va_base;
        n = PAGE_SIZE - off;
        if (n > len) {
            n = len;
        }
        memmove((void*)(ka_base + off), p, n);
        len -= n;
        p = (void*)((u64)p + n);
        va = (void*)(va_base + PAGE_SIZE);
    }
    return 0;
}

void vmmap(struct pgdir* pd, u64 va, void* ka, u64 flags) {
    *get_pte(pd, va, true) = K2P(ka) | flags;
}

void create_stack_section(struct pgdir* pd, u64 va) {
    struct section* sec = kalloc(sizeof(struct section));
	sec->flags = 0;
	sec->begin = va;
	sec->end = va + PAGE_SIZE;
	init_sleeplock(&sec->sleeplock);
	_insert_into_list(&pd->section_head, &sec->stnode);

    void* ka = alloc_page_for_user();
    memset(ka, 0, PAGE_SIZE);
    vmmap(pd, va, ka, PTE_USER_DATA);
}
