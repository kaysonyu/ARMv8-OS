#include <kernel/mem.h>
#include <kernel/sched.h>
#include <fs/pipe.h>
#include <common/string.h>

int pipeAlloc(File** f0, File** f1) {
    struct pipe* pi = NULL;
    *f0 = *f1 = NULL;

    //创建`pipe`并初始化，创建`pipe`两端的`File`放入`f0`,`f1`,分别对应读和写。
    pi = (struct pipe*)kalloc(sizeof(struct pipe));
    *f0 = filealloc();
    *f1 = filealloc();
    
    if (pi == 0 || *f0 == 0 || *f1 == 0) {
        if (pi) {
            kfree(pi);
        }
        if (*f0) {
            fileclose(*f0);
        }
        if (*f1) {
            fileclose(*f1);
        }
        return -1;
    }

    init_spinlock(&pi->lock);
    init_sem(&pi->rlock, 0);
    init_sem(&pi->wlock, 0);

    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nread = 0;
    pi->nwrite = 0;

    (*f0)->type = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = pi;

    (*f1)->type = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = pi;
  
    return 0;
}

void pipeClose(Pipe* pi, int writable) {
    //关闭`pipe`的一端。如果检测到两端都关闭，则释放`pipe`空间。
    _acquire_spinlock(&pi->lock);
    if (writable) {
        pi->writeopen = 0;
        _post_sem(&pi->rlock);
    }
    else {
        pi->readopen = 0;
        _post_sem(&pi->wlock);
    }

    if (pi->readopen == 0 && pi->writeopen == 0) {
        _release_spinlock(&pi->lock);
        kfree(pi);
    }
    else {
        _release_spinlock(&pi->lock);
    }
}

int pipeWrite(Pipe* pi, u64 addr, int n) {
    //向`pipe`写入n个byte，如果缓冲区满了则sleep。返回写入的byte数。
    int i = 0;
    struct proc *proc = thisproc();

    _acquire_spinlock(&pi->lock);
    while (i < n) { 
        if (pi->readopen == 0 || proc->killed) {
            _release_spinlock(&pi->lock);
            return -1;
        }

        //如果缓冲区满了则sleep
        if (pi->nwrite == pi->nread + PIPESIZE) {
            post_all_sem(&pi->rlock);

            get_all_sem(&pi->wlock);
            _lock_sem(&pi->wlock);
            _release_spinlock(&pi->lock);
            //sleep
            ASSERT(_wait_sem(&pi->wlock, false));
            _acquire_spinlock(&pi->lock);
        }
        else {  
            pi->data[pi->nwrite++ % PIPESIZE] = *(char*)(addr + i); 
            i++;
        }
    }
    post_all_sem(&pi->rlock);
    _release_spinlock(&pi->lock);
    return i;
}

int pipeRead(Pipe* pi, u64 addr, int n) {
    //从`pipe`中读n个byte放入addr中，如果`pipe`为空并且writeopen不为0，则sleep，否则读完pipe，返回读的byte数。
    int i;
    struct proc *proc = thisproc();
    _acquire_spinlock(&pi->lock);
    
    //如果pipe为空并且writeopen不为0，则sleep
    while (pi->nread == pi->nwrite && pi->writeopen) {
        if (proc->killed) {
            _release_spinlock(&pi->lock);
            return -1;
        }
        //sleep
        get_all_sem(&pi->rlock);
        _lock_sem(&pi->rlock);
        _release_spinlock(&pi->lock);
        ASSERT(_wait_sem(&pi->rlock, false));
        _acquire_spinlock(&pi->lock);
    }

    for (i = 0; i < n; i++) {
        if (pi->nread == pi->nwrite) {
            break;
        }
        *(char*)(addr + i) = pi->data[pi->nread++ % PIPESIZE]; 
    }
    
    post_all_sem(&pi->wlock);
    _release_spinlock(&pi->lock);
    return i;
}