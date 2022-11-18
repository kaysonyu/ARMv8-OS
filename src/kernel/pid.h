#pragma once

#include <common/spinlock.h>

#define PID_MAX 0x1000
 
typedef struct {
    unsigned int free_num;
    char map[512];
    SpinLock pidlock;
} pidmap_t;

pidmap_t pidmap;

int alloc_pid(pidmap_t* pidmap);
void free_pid(pidmap_t* pidmap, int pid);