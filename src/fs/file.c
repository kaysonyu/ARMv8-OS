/* File descriptors */

#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <fs/pipe.h>
#include <kernel/sched.h>
#include <common/string.h>
#include "fs.h"

static struct ftable ftable;
extern InodeTree inodes;
extern BlockCache bcache;

void init_ftable() {
    init_spinlock(&ftable.lock);
    memset(ftable.file, 0, NFILE*sizeof(struct file));
}

void init_oftable(struct oftable *oftable) {
    memset(oftable, 0, sizeof(struct oftable));
}

/* Allocate a file structure. */
struct file* filealloc() {
    struct file* f;
    _acquire_spinlock(&ftable.lock);
    for (f = ftable.file; f < ftable.file + NFILE; f++) {
        if (f->ref == 0) {
            f->ref = 1;
            _release_spinlock(&ftable.lock);
            return f;
        }
    }
    _release_spinlock(&ftable.lock);
    return 0;
}

/* Increment ref count for file f. */
struct file* filedup(struct file* f) {
    _acquire_spinlock(&ftable.lock);
    ASSERT(f->ref > 0);
    f->ref++;
    _release_spinlock(&ftable.lock);
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void fileclose(struct file* f) {
    struct file file_;
    _acquire_spinlock(&ftable.lock);
    ASSERT(f->ref > 0);
    f->ref--;
    if (f->ref > 0) {
        _release_spinlock(&ftable.lock);
        return;
    }
    file_ = *f;

    f->ref = 0;
    f->type = FD_NONE;
    _release_spinlock(&ftable.lock);
    
    if (file_.type == FD_PIPE) {
        pipeClose(file_.pipe, file_.writable);
    }
    else if (file_.type == FD_INODE) {
        OpContext ctx_, *ctx;
        ctx = &ctx_;
        bcache.begin_op(ctx);
        inodes.put(ctx, file_.ip);
        bcache.end_op(ctx);
    }
}

/* Get metadata about file f. */
int filestat(struct file* f, struct stat* st) {
    if (f->type == FD_INODE) {
        inodes.lock(f->ip);
        stati(f->ip, st);
        inodes.unlock(f->ip);
        return 0;
    }
    return -1;
}

/* Read from file f. */
isize fileread(struct file* f, char* addr, isize n) {
    isize r = 0;
    if (f->readable == 0) {
        return -1;
    }
    if (f->type == FD_PIPE) {
        r = pipeRead(f->pipe, (u64)addr, n);
    }
    else if (f->type == FD_INODE) {
        inodes.lock(f->ip);
        r = inodes.read(f->ip, (u8*)addr, f->off, n);
        if (r > 0) {
            f->off += r;
        }
        inodes.unlock(f->ip);
    }
    else {
        PANIC();
    }
    return r;
}

/* Write to file f. */
isize filewrite(struct file* f, char* addr, isize n) {
    isize r = 0;
    if (f->writable == 0) {
        return -1;
    }
    if (f->type == FD_PIPE) {
        r = pipeWrite(f->pipe, (u64)addr, n);
    }
    else if (f->type == FD_INODE) {
        OpContext ctx_, *ctx;
        ctx = &ctx_;
        bcache.begin_op(ctx);
        inodes.lock(f->ip);
        r = inodes.write(ctx, f->ip, (u8*)addr, f->off, n);
        if (r > 0) {
            f->off += r;
        }
        inodes.unlock(f->ip);
        bcache.end_op(ctx);
    }
    else {
        PANIC();
    }

    return r;
}
