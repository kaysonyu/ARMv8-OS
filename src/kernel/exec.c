#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>

#define MAXARG 32

extern InodeTree inodes;
extern BlockCache bcache;

static int load_seg(struct pgdir *pd, u64 va, Inode *ip, usize offset, usize sz, u64 flags);
static int fill_bss(struct pgdir *pd, u64 va, usize sz);

//static u64 auxv[][2] = {{AT_PAGESZ, PAGE_SIZE}};
extern int fdalloc(struct file* f);

int execve(const char *path, char *const argv[], char *const envp[]) {
	struct proc *p = thisproc();
	OpContext ctx_, *ctx;
	ctx = &ctx_;
	Inode *ip;
	Elf64_Ehdr elf;
	char magic[5];
	PTEntriesPtr pt;
	int i, off;
	Elf64_Phdr ph;
	u64 sp, stackbase, sz = 0;
	u64 argc = 0, ustack[MAXARG + 1];

	//重置页表
	free_pgdir(&p->pgdir);
	init_pgdir(&p->pgdir);
	init_sections(&p->pgdir.section_head);

	bcache.begin_op(ctx);

	//步骤1:从存储在' path '中的文件中加载数据
	ip = namei(path, ctx);

	inodes.lock(ip);

	inodes.read(ip, &elf, 0, sizeof(Elf64_Ehdr));

	snprintf(magic, 5, elf.e_ident);

	if (magic != ELFMAG) {
		inodes.unlock(ip);
		inodes.put(ctx, ip);
		bcache.end_op(ctx);
		return;
	}

	pt = p->pgdir.pt; 

	//步骤2:加载程序头和程序本身
	for (i = 0, off = elf.e_phoff; i < elf.e_phnum; i++, off += sizeof(Elf64_Phdr)) {
		inodes.read(ip, &ph, off, sizeof(Elf64_Phdr));

		if (ph.p_type != PT_LOAD)	continue;

		if ((ph.p_flags & PF_R) && (ph.p_flags & PF_X)) {
			load_seg(&p->pgdir, ph.p_vaddr, ip, ph.p_offset, ph.p_filesz, ST_TEXT);
		}
		else if ((ph.p_flags & PF_R) && (ph.p_flags & PF_W)) {
			load_seg(&p->pgdir, ph.p_vaddr, ip, ph.p_offset, ph.p_filesz, ST_DATA);
			fill_bss(&p->pgdir, ph.p_vaddr + ph.p_filesz, ph.p_memsz-ph.p_filesz);
		}
		else {
			PANIC();
		}

		sz = ph.p_vaddr + ph.p_memsz;
	}
	
	inodes.unlock(ip);
	inodes.put(ip, ctx);
	bcache.end_op(ctx);


	//步骤3:分配和初始化用户栈。
	stackbase = PAGE_UP(sz);
	sp = PAGE_UP(sz) + PAGE_SIZE - 32;
	create_stack_section(&p->pgdir, stackbase);

	while (argv[argc]) {
		argc++;
	}

	if (argc > MAXARG) {
		goto bad;
	}

	for (i = argc - 1; i >= 0; i--) {
		sp -= (strlen(argv[i]) + 1);
		sp -= (sp % 16);

		if (sp < stackbase) {
			goto bad;
		}
		if (copyout(&p->pgdir, sp, argv[i], strlen(argv[i]) + 1) < 0) {
			goto bad;
		}
	}
	
	sp -= 16;
	if (sp < stackbase) {
		goto bad;
	}
	*(u64*)sp = argc;

	p->ucontext->x[1] = sp;
    p->ucontext->elr = elf.e_entry;
    p->ucontext->sp_el0 = sp;                  
    arch_tlbi_vmalle1is();
    return argc;

bad:
	printk("exec_bad!\n");
    if (&p->pgdir)
        free_pgdir(&p->pgdir);
    if (ip) {
        inodes.unlock(ip);
        inodes.put(ip, ctx);
        bcache.end_op(ctx);
    }
    return -1;
}

//将程序段加载到虚拟地址va的页表中
static int load_seg(struct pgdir *pd, u64 va, Inode *ip, usize offset, usize sz, u64 flags) {
	u64 i, n;

	if (va % PAGE_SIZE != 0) {
		printk("load_seg: va must be page aligned\n");
		return -1;
	}

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

	u64 pte_flags = PTE_USER_DATA;
	pte_flags = (flags & ST_RO) ? (pte_flags | PTE_RO) : (pte_flags | PTE_RW);

	for (i = 0; i < sz; i+= PAGE_SIZE) {
		void *ka = alloc_page_for_user();
		ASSERT(ka);

		vmmap(pd, va + i, ka, pte_flags);

		if (sz - i < PAGE_SIZE) {
			n = sz -i;
		}
		else {
			n = PAGE_SIZE;
		}

		if (inodes.read(ip, (u8*)ka, offset + i, n) != n) {
			return -1;
		}
	}

	target_sec->begin = va;
	target_sec->end = PAGE_UP(va + sz);

	return 0;
}

//填充bss段
static int fill_bss(struct pgdir *pd, u64 va, usize sz) {
	u64 i, n;

	if (va % PAGE_SIZE != 0) {
		printk("fill_bss: va must be page aligned\n");
		return -1;
	}

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
		printk("load_seg: corresponding section not exist\n");
		return -1;
	}

	for (i = 0; i < sz; i += PAGE_SIZE) {
		void *ka = alloc_page_for_user();
		ASSERT(ka);

		vmmap(pd, va + i, ka, PTE_USER_DATA);

		memset(ka, 0, PAGE_SIZE);
	}

	return 0;
}
