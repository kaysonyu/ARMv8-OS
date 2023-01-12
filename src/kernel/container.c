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
    memset(container, 0, sizeof(struct container));
    container->parent = NULL;
    container->rootproc = NULL;
    init_schinfo(&container->schinfo, true);
    init_schqueue(&container->schqueue);
    // TODO: initialize namespace (local pid allocator)

    init_spinlock(&container->localpidmap.pidlock);
    _acquire_spinlock(&container->localpidmap.pidlock);
    container->localpidmap.free_num = PID_MAX;
    memset(container->localpidmap.map, 0, MAP_SIZE);
    _release_spinlock(&container->localpidmap.pidlock);
}

struct container* create_container(void (*root_entry)(), u64 arg)
{
    // TODO
    struct container* container = kalloc(sizeof(struct container));
    init_container(container);
    container->parent = thisproc()->container;
    struct proc* rootproc = create_proc();
    set_parent_to_this(rootproc);
    container->rootproc = rootproc;
    rootproc->container = container;
    
    start_proc(rootproc, root_entry, arg);
    activate_group(container);

    return container;
}

define_early_init(root_container)
{
    init_container(&root_container);
    root_container.rootproc = &root_proc;
}
