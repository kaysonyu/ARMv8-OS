#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>

SpinLock mem_lock;
RefCount alloc_page_cnt;

define_early_init(alloc_page_cnt) {
    init_rc(&alloc_page_cnt);
}

static QueueNode* pages;
extern char end[];

kmem_cache_t caches[SLAB_MAX + 1]; //4~11

define_early_init(pages) {   
    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE) {
        add_to_queue(&pages, (QueueNode*)p);  
    } 
    for (u32 i = 0; i <= SLAB_MAX; i++) {
        caches[i].order = i;
        init_list_node(&(caches[i].slabs_full));
        init_list_node(&(caches[i].slabs_partial));
    }       
}

void* kalloc_page() {
    _increment_rc(&alloc_page_cnt);
    return fetch_from_queue(&pages);
}

void kfree_page(void* p) {
    _decrement_rc(&alloc_page_cnt);
    add_to_queue(&pages, (QueueNode*)p);
}

int log2_(isize size) {
    u32 log_result;
    for (log_result = 4; log_result < 13 && (1 << log_result) < size; log_result++) {}
    return log_result;
}

void* init_slab(u32 order) {
    slab_t* new_slab = kalloc_page();
    new_slab -> used = 0;
    init_list_node(&new_slab -> ptNode);
    init_list_node(&new_slab -> objs);
    new_slab -> parent = &caches[order];

    for (ListNode* p = (ListNode*)(new_slab + 1); (u64)p < (u64)(new_slab) + PAGE_SIZE - (1 << order); p = (ListNode*)((u64)p + (1 << order))){
        _insert_into_list(&(new_slab -> objs), p);
    }

    ListNode* obj = new_slab -> objs.next;
    _detach_from_list(obj);
    new_slab -> used++;

    if (new_slab -> objs.next == &new_slab -> objs) {
        _insert_into_list(&caches[order].slabs_full, &new_slab -> ptNode);
    } 
    else {
        _insert_into_list(&caches[order].slabs_partial, &new_slab -> ptNode);
    }
    return obj;
}

void* find_in_slab(u32 order, bool* is_find) {
    ListNode* obj = 0;
    _for_in_list(slab_node, &caches[order].slabs_partial) {
        if (slab_node == &caches[order].slabs_partial) continue;

        slab_t* slab_ = container_of(slab_node, slab_t, ptNode);
        
        obj = slab_ -> objs.next;
        _detach_from_list(obj);
        slab_ -> used++;

        *is_find = true;

        if (slab_ -> objs.next == &slab_ -> objs) {
            _detach_from_list(&slab_ -> ptNode);
            _insert_into_list(&caches[order].slabs_full, &slab_ -> ptNode);
        }  

        break;
    }

    return obj;
}

void* kalloc(isize size) {
    _acquire_spinlock(&mem_lock);
    u32 order = log2_(size);
    ListNode* obj;
    bool found = false;
    obj = find_in_slab(order, &found);
    if (!found) obj = init_slab(order);
    _release_spinlock(&mem_lock);
    return (void*)obj;
}

void kfree(void* p) {   
    _acquire_spinlock(&mem_lock);

    slab_t* slab_handle = (slab_t*)((u64)p >> 12 << 12);
    _insert_into_list(&slab_handle -> objs, (ListNode*)p);
    slab_handle -> used--;

    _detach_from_list(&slab_handle -> ptNode);
    
    if (slab_handle -> used == 0) {
        kfree_page((void*)slab_handle);
    }
    else {
        _insert_into_list(&slab_handle -> parent -> slabs_partial, &slab_handle -> ptNode);
    }
    
    _release_spinlock(&mem_lock);
}
