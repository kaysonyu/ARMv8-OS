#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/container.h>
#include <kernel/paging.h>

struct proc root_proc;
extern struct container root_container;
extern InodeTree inodes;

void kernel_entry();
void proc_entry();

static SpinLock plock;

define_early_init(plock) {
    init_spinlock(&plock);
}

void set_parent_to_this(struct proc* proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    _acquire_spinlock(&plock);
    proc->parent = thisproc();
    _insert_into_list(&thisproc()->children, &proc->ptnode);
    _release_spinlock(&plock);

}

NO_RETURN void exit(int code)
{
    // TODO
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the rootproc of the container, and notify the it if there is zombie
    // 4. notify the parent
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    _acquire_spinlock(&plock);
    auto this = thisproc();
    ASSERT(this != this->container->rootproc && !this->idle);
    this -> exitcode = code;

    free_pgdir(&(this->pgdir));

    while (!_empty_list(&(this -> children))) {
        ListNode* child_node = (this -> children).next; 
        _detach_from_list(child_node);
        
        proc* child_proc = container_of(child_node, proc, ptnode);
        proc* rootproc = child_proc -> container -> rootproc;
        // printk("child_node: %p, child_proc: %p, pid: %d\n", child_node, child_proc, child_proc->pid);
        child_proc -> parent = rootproc;
        _insert_into_list(&rootproc->children, &child_proc->ptnode);

        if (is_zombie(child_proc)) {
            post_sem(&rootproc->childexit);
        }
    }
    // _release_spinlock(&plock);
    post_sem(&(this -> parent ->childexit));
    _acquire_sched_lock();
    _release_spinlock(&plock);
    _sched(ZOMBIE);
    // _release_sched_lock();
    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int* exitcode, int* pid)
{
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its local pid and exitcode
    // NOTE: be careful of concurrency
    auto this = thisproc();
    if (_empty_list(&(this -> children))) {
        // _release_spinlock(&plock);
        return -1;
    }

    bool sig = wait_sem(&(this -> childexit));
    if (sig == false) {
        return -2;
    }

    _acquire_spinlock(&plock);
    int w_pid = 0;
    _for_in_list(child_node, &(this -> children)) {
        if (child_node == &(this -> children)) {
            continue;
        }

        proc* child_proc = container_of(child_node, proc, ptnode);

        if (is_zombie(child_proc)) {
            
            _detach_from_list(child_node);

            *exitcode = child_proc -> exitcode;
            *pid = child_proc -> pid;
    
            w_pid = child_proc -> localpid;

            free_pid(&child_proc->container->localpidmap, child_proc -> localpid);
            free_pid(&pidmap, child_proc -> pid);
            kfree_page(child_proc -> kstack);
            kfree(child_proc);
            break;
            // _release_spinlock(&plock);
        }
    }
    _release_spinlock(&plock);
    return w_pid;
}

proc* traverse_proc(proc *p, int pid) {  
    _for_in_list(child_node, &p -> children) {
        if (child_node == &p -> children) continue;
        proc* child = container_of(child_node, proc, ptnode);
        if (child -> pid == pid) 
            return child;
        else {
            proc* obj = traverse_proc(child, pid);
            if (obj != NULL) {
                return obj;
            }
        }    
    }
    return NULL;
}

int kill(int pid) {
    // TODO
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    _acquire_spinlock(&plock);
    proc* target_proc = traverse_proc(&root_proc, pid);
    if (target_proc == NULL || is_unused(target_proc)) {
        _release_spinlock(&plock);
        return -1;
    }

    target_proc -> killed = true;
    alert_proc(target_proc);

    _release_spinlock(&plock);
    return 0;
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg)
{
    // TODO
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its local pid
    // NOTE: be careful of concurrency
    if (p -> parent == NULL) {
        _acquire_spinlock(&plock);
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
        _release_spinlock(&plock);
    }
    p->kcontext->lr = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    p->localpid = alloc_pid(&p->container->localpidmap);
    // printk("pid:%d\n", p->localpid);
    activate_proc(p);
    return p->localpid;
}

void init_proc(struct proc* p)
{
    // TODO
    // setup the struct proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    memset(p, 0, sizeof(*p));
    p->killed = false;
    p->pid = alloc_pid(&pidmap);
    // printk("PID:%d\n", p->pid);
    p->state = UNUSED;
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_schinfo(&p->schinfo, false);
    init_pgdir(&(p->pgdir));
    p->container = &root_container;
    p->kstack = kalloc_page();
    p->kcontext = (KernelContext*)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(KernelContext) - sizeof(UserContext));
    p->ucontext = (UserContext*)((u64)p->kstack + PAGE_SIZE - 16 -sizeof(UserContext));
}

struct proc* create_proc()
{
    struct proc* p = kalloc(sizeof(struct proc));
    init_proc(p);
    return p;
}

struct proc* traverse_find_offline(struct container* container) { 
    struct proc* find_proc = NULL;
    _for_in_list(p, &container->schqueue.rq) {
        if (p == &container->schqueue.rq) {
            continue;
        }
        struct schinfo* schinfo_ = container_of(p, struct schinfo, rq_node);
        if (!schinfo_->iscontainer) {
            struct proc* proc = container_of(schinfo_, struct proc, schinfo);
            if (!proc->pgdir.online) {
                find_proc = proc;
                break;
            }
        }
        else {
            find_proc = traverse_find_offline(container_of(schinfo_, struct container, schinfo));
        }
    }
    return find_proc;
}

struct proc* get_offline_proc() {
    struct proc* proc = traverse_find_offline(&root_container);
    if (proc == NULL)   PANIC();
    _acquire_spinlock(&proc->pgdir.lock);
    return proc;
}


define_init(root_proc)
{
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork() {
    int pid;
    struct proc *p = thisproc();
    struct proc *fork_p = create_proc();

    fork_p->killed = p->killed;
    fork_p->idle = p->idle;

    set_parent_to_this(fork_p);
    set_container_to_this(fork_p);

    memmove(fork_p->ucontext, p->ucontext, sizeof(UserContext));
    fork_p->ucontext->x[0] = 0;

    for (int i = 0; i < NOFILE; i++) {
        if (p->oftable.ofile[i]) {
            fork_p->oftable.ofile[i] = filedup(p->oftable.ofile[i]);
        }
    }
    if (fork_p->cwd) {
        fork_p->cwd = inodes.share(p->cwd);
    }

    _for_in_list(node, &p->pgdir.section_head) {
        if (node == &p->pgdir.section_head)  continue;

        struct section* p_sec = container_of(node, struct section, stnode);

        struct section* c_sec = kalloc(sizeof(struct section));
        memmove(c_sec, p_sec, sizeof(struct section));
        init_sleeplock(&c_sec->sleeplock);
        _insert_into_list(&fork_p->pgdir.section_head, &c_sec->stnode);

        for (u64 va = p_sec->begin; va < p_sec->end; va += PAGE_SIZE) {
            PTEntriesPtr pte = get_pte(&p->pgdir, va, false);
            u64 flags = PTE_FLAGS(*pte);

            void* ka = alloc_page_for_user();
            memmove(ka, (void*)va, PAGE_SIZE);

            vmmap(&fork_p->pgdir, va, ka, flags);
        }
    }

    pid = fork_p->pid;

    start_proc(fork_p, trap_return, 0);

    return pid;
}
