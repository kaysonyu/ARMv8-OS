#pragma once

#include <common/defines.h>
#include <aarch64/mmu.h>
#include <common/list.h>

void* kalloc_page();
void kfree_page(void*);

void* kalloc(isize);
void kfree(void*);

#define SLAB_MAX 11

typedef struct kmem_cache {
    u32 order;
    ListNode slabs_partial;    
    ListNode slabs_full;
} kmem_cache_t;

typedef struct slab {
    kmem_cache_t* parent;
    u32 used;
    ListNode s_mem;
    ListNode next_slab;
} slab_t;
