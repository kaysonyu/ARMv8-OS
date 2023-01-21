#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/init.h>
#include <kernel/sched.h>
#include <kernel/paging.h>
#include <test/test.h>
#include <fs/cache.h>
#include <driver/sd.h>

bool panic_flag;

extern char icode[], eicode[];
void trap_return();

NO_RETURN void idle_entry() {
    set_cpu_on();
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap {
            arch_wfi();
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

static void create_user_proc() {
    auto p = create_proc();
    for (u64 q = (u64)icode; q < (u64)eicode; q += PAGE_SIZE) {
        vmmap(&p->pgdir, 0x400000 + q - (u64)icode, (void*)q, PTE_USER_DATA);
    }
    struct section* section = create_section(&p->pgdir.section_head, ST_TEXT);
    section->begin = PAGE_BASE((u64)icode);
    section->end = PAGE_UP((u64)eicode);
    ASSERT(p->pgdir.pt);
    p->ucontext->x[0] = 0;
    p->ucontext->elr = 0x400000;
    p->ucontext->spsr = 0;
    set_parent_to_this(p);
    set_container_to_this(p);
    start_proc(p, trap_return, 0);
}

NO_RETURN void kernel_entry() {
    printk("hello world %d\n", (int)sizeof(struct proc));
    
    do_rest_init();

    // TODO: map init.S to user space and trap_return to run icode
    create_user_proc();
    while (1) {
        yield();
        if (panic_flag) {
            break;
        }
        arch_with_trap {
            arch_wfi();
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

NO_INLINE NO_RETURN void _panic(const char* file, int line) {
    printk("=====%s:%d PANIC%d!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}
