#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/paging.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>

#define MAXARG 32
#define STACK_BASE 0x60000000 

extern InodeTree inodes;
extern BlockCache bcache;

static int load_seg(struct pgdir *pd, u64 va, Inode *ip, usize offset, usize sz, u64 flags);
static int fill_bss(struct pgdir *pd, u64 va, usize sz);

//static u64 auxv[][2] = {{AT_PAGESZ, PAGE_SIZE}};
extern int fdalloc(struct file* f);

int execve(const char *path, char *const argv[], char *const envp[]) {
	// printk("in execve\n");
	struct proc *p = thisproc();
	OpContext ctx_, *ctx;
	ctx = &ctx_;
	Inode *ip;
	Elf64_Ehdr elf;
	int i, off;
	Elf64_Phdr ph;
	u64 sp, stackbase;
	u64 argc = 0;
	struct pgdir pd;
	u64 ustack[MAXARG+2];

	ASSERT((u64)envp || true);

	bcache.begin_op(ctx);

	//步骤1:从存储在' path '中的文件中加载数据
	if ((ip = namei(path, ctx)) == NULL) {
		printk("execve: path invalid");
		bcache.end_op(ctx);
		return -1;
	};

	inodes.lock(ip);

	if (inodes.read(ip, (u8*)&elf, 0, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
		goto bad;
	}

	if (strncmp((char*)elf.e_ident, ELFMAG, 4) != 0) {
		inodes.unlock(ip);
		inodes.put(ctx, ip);
		bcache.end_op(ctx);
		return -1;
	}

	//重置section
	// free_sections(&p->pgdir);
	// create_file_sections(&p->pgdir.section_head);

	init_pgdir(&pd);
	create_file_sections(&pd.section_head);

	//步骤2:加载程序头和程序本身
	for (i = 0, off = elf.e_phoff; i < elf.e_phnum; i++, off += sizeof(Elf64_Phdr)) {
		if ((inodes.read(ip, (u8*)&ph, off, sizeof(Elf64_Phdr))) != sizeof(Elf64_Phdr)) {
			printk("execve: read phdr fail");
			goto bad;
		}

		if (ph.p_type != PT_LOAD)	continue;

		if ((ph.p_flags & PF_R) && (ph.p_flags & PF_X)) {
			if (load_seg(&pd, ph.p_vaddr, ip, ph.p_offset, ph.p_filesz, ST_TEXT) < 0) {
				goto bad;
			}
			// printk("phvaddr: %llx--max: %llx\n", (u64)ph.p_vaddr, (u64)ph.p_vaddr+ph.p_memsz);
		}
		else if ((ph.p_flags & PF_R) && (ph.p_flags & PF_W)) {
			if (load_seg(&pd, ph.p_vaddr, ip, ph.p_offset, ph.p_filesz, ST_DATA) < 0) {
				goto bad;
			}
			if (fill_bss(&pd, ph.p_vaddr + ph.p_filesz, ph.p_memsz-ph.p_filesz) < 0) {
				goto bad;
			}
			// printk("phvaddr: %llx--max: %llx\n", (u64)ph.p_vaddr, (u64)ph.p_vaddr+ph.p_memsz);
		}
		else {
			goto bad;
		}

		// sz = ph.p_vaddr + ph.p_memsz;
	}
	
	inodes.unlock(ip);
	inodes.put(ctx, ip);
	bcache.end_op(ctx);

	//步骤3:分配和初始化用户栈。
	// stackbase = PAGE_UP(sz);
	stackbase = STACK_BASE;
	sp = stackbase + PAGE_SIZE - 32;
	create_stack_section(&pd, stackbase);

	if (argv != NULL) {
		for (argc = 0; argv[argc]; argc++) {
			if (argc >= MAXARG) {
				goto bad;
			}
			sp -= (strlen(argv[argc]) + 1);
			sp -= (sp % 16);
			if (sp < stackbase) {
				goto bad;
			}
			if (copyout(&pd, (void*)sp, argv[argc], strlen(argv[argc]) + 1) < 0) {
				goto bad;
			}
			ustack[argc + 1] = sp;
		}
	}
	
	ustack[argc + 1] = 0;
	ustack[0] = argc;

	sp -= (argc + 2) * sizeof(u64);
	sp -= sp % 16;
	if (sp < stackbase) {
		goto bad;
	}
	if (copyout(&pd, (void*)sp, (u64*)ustack, (argc + 2) * sizeof(u64)) < 0) {
		goto bad;
	}

	// while (argv[argc]) {
	// 	argc++;
	// }

	// if (argc > MAXARG) {
	// 	goto bad;
	// }

	// for (i = argc - 1; i >= 0; i--) {
	// 	sp -= (strlen(argv[i]) + 1);
	// 	sp -= (sp % 16);

	// 	if (sp < stackbase) {
	// 		goto bad;
	// 	}
	// 	if (copyout(&pd, (void*)sp, argv[i], strlen(argv[i]) + 1) < 0) {
	// 		goto bad;
	// 	}
	// }
	
	// sp -= 16;
	// if (sp < stackbase) {
	// 	goto bad;
	// }
	// *(u64*)sp = argc;
	
	free_pgdir(&p->pgdir);
	init_pgdir(&p->pgdir);
	copy_pgdir(&pd, &p->pgdir);

	free_pgdir(&pd);

    p->ucontext->elr = elf.e_entry;
    p->ucontext->sp_el0 = sp;  

    init_oftable(&p->oftable); 

    attach_pgdir(&p->pgdir);
    arch_fence();
    arch_tlbi_vmalle1is();

	// printk("exec end\n");
   
    return argc;

bad:
	printk("exec_bad!\n");
    free_pgdir(&pd);
    if (ip) {
        inodes.unlock(ip);
        inodes.put(ctx, ip);
        bcache.end_op(ctx);
    }
    return -1;
}

//将程序段加载到虚拟地址va的页表中
static int load_seg(struct pgdir *pd, u64 va, Inode *ip, usize offset, usize sz, u64 flags) {
	u64 n;
	u64 begin, end;
	u64 p;
	u64 file_off, page_off;

	begin = PAGE_BASE(va);
	end = PAGE_UP(va + sz);
	p = begin;

	struct section *target_sec = NULL;
	_for_in_list(node, &pd->section_head) {
		if (node == &pd->section_head)	continue;

		struct section *sec = container_of(node, struct section, stnode);
		if (sec->flags == flags) {
			target_sec = sec;
			break;
		}
	}

	if (target_sec == NULL) {
		printk("load_seg: corresponding section not exist\n");
		return -1;
	}

	target_sec->begin = begin;
	target_sec->end = end;

	u64 pte_flags = PTE_USER_DATA;
	pte_flags = (flags & ST_RO) ? (pte_flags | PTE_RO) : (pte_flags | PTE_RW);
	while (p < end) {
		file_off = (p < va) ? 0 : (p - va);
		page_off = (p < va) ? (va - p) : 0;
		n = (p + PAGE_SIZE > va + sz) ? (va + sz - p - page_off) : (PAGE_SIZE - page_off);

		void *ka = alloc_page_for_user();
		ASSERT(ka);
		vmmap(pd, p, ka, pte_flags);

		if (inodes.read(ip, (u8*)ka + page_off, offset + file_off, n) != n) {
			return -1;
		}

		p += PAGE_SIZE;
	}

	return 0;
}

//填充bss段
static int fill_bss(struct pgdir *pd, u64 va, usize sz) {
	u64 begin, end;
	u64 p;
	u64 page_off;

	begin = PAGE_BASE(va);
	end = PAGE_UP(va + sz);
	p = begin;

	struct section *target_sec = NULL;
	_for_in_list(node, &pd->section_head) {
		if (node == &pd->section_head)	continue;

		struct section *sec = container_of(node, struct section, stnode);
		if (sec->flags == ST_BSS) {
			target_sec = sec;
			break;
		}
	}

	if (target_sec == NULL) {
		printk("bss_seg: corresponding section not exist\n");
		return -1;
	}

	target_sec->end = end;

	u64 pte_flags = PTE_USER_DATA | PTE_RW;
	while (p < end) {
		if (p < va) {
			page_off = va - p;
			PTEntriesPtr pte = get_pte(pd, p, false);
			void *ka = (void*)P2K(PTE_ADDRESS(*pte));
			memset(ka + page_off, 0, PAGE_SIZE - page_off);
        }
		else {
			void *ka = alloc_page_for_user();
			vmmap(pd, p, ka, pte_flags);
			memset(ka, 0, PAGE_SIZE);
		}
		p += PAGE_SIZE;
	}

	return 0;
}
