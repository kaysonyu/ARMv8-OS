#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/init.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <driver/sd.h>
#include <fs/cache.h>

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

NO_RETURN void kernel_entry() {
    printk("hello world %d\n", (int)sizeof(struct proc));
    
    do_rest_init();

    // TODO: map init.S to user space and trap_return to run icode
    struct proc* p = thisproc();
    for (u64 q = (u64)icode; q < (u64)eicode; q += PAGE_SIZE) {
        vmmap(&p->pgdir, 0x400000 + q - (u64)icode, (void*)q, PTE_USER_DATA);
    }
    p->ucontext->elr = 0x400000;
    p->ucontext->spsr = 0;

    arch_tlbi_vmalle1is();
    trap_return();

    PANIC();
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
