#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <driver/clock.h>
#include <kernel/container.h>

extern bool panic_flag;
extern struct container root_container;

extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);

static SpinLock schlock;

extern struct timer sched_timer[4];

define_early_init(schlock) {
    init_spinlock(&schlock);
}

define_init(sched) {
    for (int i = 0; i < NCPU; i++) {
        struct proc* p = kalloc(sizeof(struct proc));
        p->idle = 1;
        p->pid = 0;
        p->killed = false;
        p->state = RUNNING;
        cpus[i].sched.thisproc = cpus[i].sched.idle = p;
    }
}

void init_schqueue(struct schqueue* queue) {
    init_list_node(&queue->rq);
}

struct proc* thisproc()
{
    // TODO: return the current process
    return cpus[cpuid()].sched.thisproc;

}

void init_schinfo(struct schinfo* p, bool group)
{
    // TODO: initialize your customized schinfo for every newly-created process
    init_list_node(&p -> rq_node);
    p -> start_ = 0;
    p -> occupy_ = 0;
    p -> iscontainer = group;
}

void _acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need
    _acquire_spinlock(&schlock);

}

void _release_sched_lock()
{
    // TODO: release the sched_lock if need
    _release_spinlock(&schlock);

}

bool is_zombie(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool is_unused(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == UNUSED;
    _release_sched_lock();
    return r;
}

bool _activate_proc(struct proc* p, bool onalert)
{
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    // else: panic
    _acquire_sched_lock();
    if (p->state == RUNNING || p->state == RUNNABLE) {
        _release_sched_lock();
        return false;
    }
    else if (p->state == SLEEPING || p->state == UNUSED || (p->state == DEEPSLEEPING && !onalert)) {
        p->state = RUNNABLE;
        _insert_into_list(&p->container->schqueue.rq, &p->schinfo.rq_node);
    }
    else {
        _release_sched_lock();
        return false;
    }
    _release_sched_lock();
    return true;

}

void activate_group(struct container* group)
{
    // TODO: add the schinfo node of the group to the schqueue of its parent
    _acquire_sched_lock();
    _insert_into_list(&group->parent->schqueue.rq, &group->schinfo.rq_node);
    _release_sched_lock();
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    auto this = thisproc();
    if (new_state == RUNNABLE && this != cpus[cpuid()].sched.idle) {
        _insert_into_list(&this->container->schqueue.rq, &(this -> schinfo.rq_node)); 
    }
    this -> state = new_state;

    if (this == cpus[cpuid()].sched.idle) {
        return;
    }

    int now_occupy = get_timestamp() - (this -> schinfo).start_;
    (this -> schinfo).occupy_ += now_occupy;
    struct container* parent = this->container;
    while (parent != &root_container) {
        parent->schinfo.occupy_ += now_occupy;
        parent = parent->parent;
    }
}

bool not_empty_container(struct container* container) {  
    bool not_empty_flag = false;
    _for_in_list(child_node, &container->schqueue.rq) {
        if (child_node == &container->schqueue.rq) continue;

        struct schinfo* schinfo_ = container_of(child_node, struct schinfo, rq_node);
        // auto schunit = (schinfo->iscontainer) ? container_of(child_node, struct container, schinfo.rq_node) : container_of(child_node, struct proc, schinfo.rq_node);

        if (schinfo_->iscontainer) {
            not_empty_flag = not_empty_container(container_of(&schinfo_->rq_node, struct container, schinfo.rq_node));
        }
        else {
            not_empty_flag = true;
        }

        if (not_empty_flag) {
            break;
        }
    }
    return not_empty_flag;
}

proc* traverse_queue(struct container* container) { 
    // struct proc* schproc = NULL;
    // struct container* schcontainer = NULL;
    int min_ = -1;
    struct schinfo* next_schinfo = NULL;
    _for_in_list(p, &container->schqueue.rq) {
        if (p == &container->schqueue.rq) {
            continue;
        }
        struct schinfo* schinfo_ = container_of(p, struct schinfo, rq_node);
        // auto schunit = (schinfo->iscontainer) ? container_of(p, struct container, schinfo.rq_node) : container_of(p, struct proc, schinfo.rq_node);
        // printk("pid_all: %d\n", proc->pid);
        if (schinfo_->iscontainer && !not_empty_container(container_of(&schinfo_->rq_node, struct container, schinfo.rq_node)))   {
            continue;
        }

        int occupy_time = schinfo_->occupy_;
        if (min_ == -1 || (occupy_time < min_)) {
            min_ = occupy_time;
            next_schinfo = schinfo_;
        }
    }

    if (next_schinfo == NULL) {
        return NULL;
    }
    if (next_schinfo->iscontainer) {
        return traverse_queue(container_of(next_schinfo, struct container, schinfo));
    }
    else {
        return container_of(next_schinfo, struct proc, schinfo);
    }
}


extern bool panic_flag;
static struct proc* pick_next()
{
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    // _acquire_sched_lock();
    if (panic_flag)
        return cpus[cpuid()].sched.idle;
    struct proc* next_proc = traverse_queue(&root_container);
    if (next_proc == NULL) {
        next_proc = cpus[cpuid()].sched.idle;
    }
    else {
        _detach_from_list(&(next_proc -> schinfo.rq_node));
    }
    // _release_sched_lock();
    return next_proc;
}

static void update_this_proc(struct proc* p)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    // reset_clock(1000);
    if (!sched_timer[cpuid()].triggered) cancel_cpu_timer(&sched_timer[cpuid()]);
    set_cpu_timer(&sched_timer[cpuid()]);

    cpus[cpuid()].sched.thisproc = p;
    p -> schinfo.start_ = get_timestamp();

    // struct container* parent = thisproc()->container;
    // while (parent != &root_container) {
    //     parent->schinfo.start_ = p->schinfo.start_;
    //     parent = parent->parent;
    // }
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state)
{
    auto this = thisproc();
    // printk("ppp\n");
    if (this -> killed && new_state != ZOMBIE) {
        _release_sched_lock();
        return;
    }
    // printk("ppp\n");
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    // printk("this.pid:%d, next.pid:%d\n", this->pid, next->pid);
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this) {
        attach_pgdir(&(next -> pgdir));
        swtch(next->kcontext, &this->kcontext);
    }
    _release_sched_lock();
}

__attribute__((weak, alias("simple_sched"))) void _sched(enum procstate new_state);

u64 proc_entry(void(*entry)(u64), u64 arg)
{
    _release_sched_lock();
    set_return_addr(entry);
    return arg;
}



