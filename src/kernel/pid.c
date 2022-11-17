#include <kernel/pid.h>
#include <kernel/init.h>
#include <common/string.h>
#include <kernel/printk.h>

int alloc_pid() {
    if (!pidmap.free_num) {
        return -1;
    }
    int pid_ = 1;
    int *p; int mask;
    while (pid_ < PID_MAX) {
        p = ((int*)(&pidmap.map)) + (pid_ >> 5);
        mask = 1 << (pid_ & 31);
        if (~(*p) & mask) {
            *p = *p | mask; break;
        }
        pid_++;
    }
    if (pid_ < PID_MAX) {
        pidmap.free_num--;
        return pid_;
    }
    return -1;
}


void free_pid(int pid) {
    int *p = ((int*)(&pidmap.map)) + (pid >> 5);
    int mask = 1 << (pid & 31);
    *p = *p & ~mask;
    pidmap.free_num++;
}

define_init(pidmap) {
    pidmap.free_num = PID_MAX;
    memset(pidmap.map, 0, 4096);
}
