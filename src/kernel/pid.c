#include <kernel/pid.h>
#include <kernel/init.h>
#include <common/string.h>
#include <kernel/printk.h>

int alloc_pid(pidmap_t* pidmap) {
    _acquire_spinlock(&pidmap->pidlock);
    if (!pidmap->free_num) {
        _release_spinlock(&pidmap->pidlock);
        return -1;
    }
    int pid_ = 1;
    int *p; int mask;
    while (pid_ < PID_MAX) {
        p = ((int*)(&pidmap->map)) + (pid_ >> 5);
        mask = 1 << (pid_ & 31);
        if (~(*p) & mask) {
            *p = *p | mask; break;
        }
        pid_++;
    }
    if (pid_ < PID_MAX) {
        pidmap->free_num--;
        _release_spinlock(&pidmap->pidlock);
        return pid_;
    }
    _release_spinlock(&pidmap->pidlock);
    return -1;
}


void free_pid(pidmap_t* pidmap, int pid) {
    _acquire_spinlock(&pidmap->pidlock);
    int *p = ((int*)(&pidmap->map)) + (pid >> 5);
    int mask = 1 << (pid & 31);
    *p = *p & ~mask;
    pidmap->free_num++;
    _release_spinlock(&pidmap->pidlock);
}

define_init(pidmap) {
    init_spinlock(&pidmap.pidlock);

    _acquire_spinlock(&pidmap.pidlock);
    pidmap.free_num = PID_MAX;
    memset(pidmap.map, 0, 512);
    _release_spinlock(&pidmap.pidlock);
}
