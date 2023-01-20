#pragma once

#include <kernel/proc.h>
#include <aarch64/mmu.h>

#define ST_FILE   1
#define ST_SWAP  (1<<1)
#define ST_RO    (1<<2)
#define ST_HEAP  (1<<3)
#define ST_TEXT  (ST_FILE | ST_RO)
#define ST_DATA   ST_FILE 
#define ST_BSS    ST_FILE	

struct section{
    u64 flags;
    SleepLock sleeplock;
    u64 begin;
    u64 end;
    ListNode stnode;
    // File* fp;  //pointer to file struct
    // u64 offset;    //the offset in file
    // u64 length; //the length of mapped content in file
};

int pgfault(u64 iss);
void swapout(struct pgdir* pd, struct section* st);
void swapin(struct pgdir* pd, struct section* st);
void* alloc_page_for_user();
struct section* create_section(ListNode* section_head, u64 flags);
void create_file_sections(ListNode* section_head);
void free_sections(struct pgdir* pd);
u64 sbrk(i64 size);
struct section* get_heap(struct pgdir* pd);
