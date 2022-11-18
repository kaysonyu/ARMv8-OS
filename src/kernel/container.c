#include <common/string.h>
#include <common/list.h>
#include <kernel/container.h>
#include <kernel/init.h>
#include <kernel/printk.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/pid.h>

struct container root_container;
extern struct proc root_proc;

void activate_group(struct container* group);

void set_container_to_this(struct proc* proc)
{
    proc->container = thisproc()->container;
}

void init_container(struct container* container)
{
    // printk("~~~container_%p\n", thisproc()->container);
    memset(container, 0, sizeof(struct container));
    // printk("~~~container_%p-----%p\n", container, &container->schqueue);
    container->parent = NULL;
    container->rootproc = NULL;
    // u32 uu = sizeof(struct proc);
    // printk("~~~container_%p_%p_%p_%d\n", thisproc(), &thisproc()->container, &root_container, uu);
    init_schinfo(&container->schinfo, true);
    init_schqueue(&container->schqueue);
    // TODO: initialize namespace (local pid allocator)

    init_spinlock(&container->localpidmap.pidlock);
    _acquire_spinlock(&container->localpidmap.pidlock);
    container->localpidmap.free_num = PID_MAX;
    memset(container->localpidmap.map, 0, 512);
    _release_spinlock(&container->localpidmap.pidlock);
}

struct container* create_container(void (*root_entry)(), u64 arg)
{
    // TODO
    // printk("container_%p\n", thisproc()->container);
    struct container* container = kalloc(sizeof(struct container));
    // u64 p = sizeof(pidmap_t);
    // u64 c = sizeof(struct container);
    // printk("~~~container_%p--%p---%lld--%lld\n", container, &container->localpidmap, c, p);
    init_container(container);
    // printk("~~~container_%p\n", thisproc()->container);
    // container->parent = thisproc()->container;
    container->parent = thisproc()->container;
    // printk("???%d,%p\n", thisproc()->pid, thisproc()->container);
    struct proc* rootproc = create_proc();
    // printk("donee\n");
    set_parent_to_this(rootproc);
    container->rootproc = rootproc;

    // printk("donee\n");
    
    start_proc(rootproc, root_entry, arg);
    // printk("donee\n");
    activate_group(container);

    // printk("donee\n");
    return container;
}

define_early_init(root_container)
{
    init_container(&root_container);
    root_container.rootproc = &root_proc;
}
