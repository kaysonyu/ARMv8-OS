#pragma once
#include <sys/syscall.h>

#define SYS_myreport 499
#define SYS_pstat 500
#define SYS_sbrk 12

#define SYS_clone 220
#define SYS_myexit 457
#define SYS_myyield 459

#define SYS_exit 93
#define SYS_execve 221

#define SYS_tid_address 96
#define SYS_ioctl 29
#define SYS_sigprocmask 135
#define SYS_wait4 260
#define SYS_exit_group 94
#define SYS_unlinkat 35

// // System call numbers
// #define SYS_fork    1
// #define SYS_exit    2
// #define SYS_wait    3
// #define SYS_pipe    4
// //
// #define SYS_read    5
// #define SYS_kill    6
// #define SYS_exec    7
// //
// #define SYS_fstat   8
// //
// #define SYS_chdir   9
// //
// #define SYS_dup    10
// #define SYS_getpid 11
// #define SYS_sbrk   12
// #define SYS_sleep  13
// #define SYS_uptime 14
// #define SYS_open   15
// //
// #define SYS_write  16
// #define SYS_mknod  17
// //
// #define SYS_unlink 18
// #define SYS_link   19
// #define SYS_mkdir  20
// //
// #define SYS_close  21

// //ioctl
// //mmap
// //munmap
// //writev
// //newfstatat
// //openat
// //mkdirat
// //mknodat
// //pipe2


// // find more in musl/arch/aarch64/bits/syscall.h