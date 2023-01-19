#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <kernel/schinfo.h>
#include <kernel/pt.h>
#include <kernel/container.h>
#include <fs/inode.h>
#include <fs/file.h>

#define NOFILE 1024 /* open files per process */

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, DEEPSLEEPING, ZOMBIE };

typedef struct UserContext
{
    // TODO: customize your trap frame
    /*
    spsr: 保存进程在系统调用之前的状态寄存器。
    elr: 存储程序在系统调用之前的返回地址。
    lr: 保存程序在系统调用之前的链接寄存器。
    sp_el0: 用于存储当前进程在系统调用之前的用户空间栈指针。
    x[18]: 存储程序在系统调用之前的通用寄存器。
    q0[2]: 用于存储程序在系统调用之前的128位Q寄存器。
    tpidr_el0: 用于存储程序在系统调用之前的线程 ID 寄存器。
    */
    u64 tpidr_el0;
    u64 q0[2];
    u64 spsr, elr, lr, sp_el0;
    u64 x[18];
    
} UserContext;

typedef struct KernelContext
{
    // TODO: customize your context
    /*
    lr：连接寄存器，用于在函数调用返回时存储返回地址。
    x0 - x1：通用寄存器，可用于存储临时数据。
    x19-x29：通用寄存器，可用于存储长期数据和调用其他函数时传递参数。
    */
    u64 lr, x0, x1;
    u64 x[11];  //x19-x29

} KernelContext;

typedef struct proc
{
    bool killed;
    bool idle;
    int pid;
    int localpid;
    int exitcode;
    enum procstate state;
    Semaphore childexit;
    ListNode children;
    ListNode ptnode;
    struct proc* parent;
    struct schinfo schinfo;
    struct pgdir pgdir;
    struct container* container;
    void* kstack;
    UserContext* ucontext;
    KernelContext* kcontext;
    struct oftable oftable;
    Inode* cwd; // current working dictionary
} proc;

// void init_proc(struct proc*);
WARN_RESULT struct proc* create_proc();
void set_parent_to_this(struct proc*);
int start_proc(struct proc*, void(*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int* exitcode, int* pid);
WARN_RESULT int kill(int pid);
WARN_RESULT int fork();
struct proc* get_offline_proc();
