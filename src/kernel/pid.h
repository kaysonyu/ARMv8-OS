#pragma once

#include <common/spinlock.h>

#define PID_MAX 0x1000
#define MAP_SIZE 512
 
typedef struct {
    unsigned int free_num;
    char map[MAP_SIZE];
    SpinLock pidlock;
} pidmap_t;

pidmap_t pidmap;

int alloc_pid(pidmap_t* pidmap);
void free_pid(pidmap_t* pidmap, int pid);