#pragma once

#include <common/defines.h>
#include <aarch64/mmu.h>
#include <common/rc.h>

#define REVERSED_PAGES 1024 //Reversed pages
#define SLAB_MAX 11

struct page{
	RefCount ref;
};

WARN_RESULT void* kalloc_page();
void kfree_page(void*);

WARN_RESULT void* kalloc(isize);
void kfree(void*);

u64 left_page_cnt();
WARN_RESULT void* get_zero_page();
bool check_zero_page();
u32 write_page_to_disk(void* ka);
void read_page_from_disk(void* ka, u32 bno);

typedef struct kmem_cache {
    u32 order;
    ListNode slabs_partial;    
    ListNode slabs_full;
} kmem_cache_t;

typedef struct slab {
    kmem_cache_t* parent;
    u32 used;
    ListNode objs;
    ListNode ptNode;
} slab_t;
