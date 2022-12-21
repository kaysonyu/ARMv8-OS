#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <common/string.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>
#include <fs/defines.h>
#include <fs/block_device.h>
#include <fs/cache.h>


SpinLock mem_lock;

u64 left_page_num;
SpinLock left_page_lk;
RefCount alloc_page_cnt;

define_early_init(alloc_page_cnt) {
    init_rc(&alloc_page_cnt);
}


static QueueNode* pages;
extern char end[];

struct page pages_info[PAGE_NUM];

kmem_cache_t caches[SLAB_MAX + 1]; //4~11

define_early_init(pages) {   
    init_spinlock(&left_page_lk);
    left_page_num = 0;

    // memset(pages_info, 0, PAGE_NUM*sizeof(struct page));
    // for (int i = 0; i < PAGE_NUM; i++) {
    //     init_rc(&pages_info[i].ref);
    // }
    u64 zero_page = PAGE_BASE((u64)&end) + PAGE_SIZE;
    memset((u8*)zero_page, 0, PAGE_SIZE);
    init_rc(&pages_info[K2P(zero_page)/PAGE_SIZE].ref);
    _increment_rc(&pages_info[K2P(zero_page)/PAGE_SIZE].ref);

    for (u64 p = PAGE_BASE((u64)&end) + 2 * PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE) {
        add_to_queue(&pages, (QueueNode*)p);  
        left_page_num++;
    } 
    for (u32 i = 0; i <= SLAB_MAX; i++) {
        caches[i].order = i;
        init_list_node(&(caches[i].slabs_full));
        init_list_node(&(caches[i].slabs_partial));
    }       
}

void* kalloc_page() {
    // _increment_rc(&alloc_page_cnt);
    QueueNode* page = fetch_from_queue(&pages);
    init_rc(&pages_info[K2P(page)/PAGE_SIZE].ref);
    _increment_rc(&pages_info[K2P(page)/PAGE_SIZE].ref);

    _acquire_spinlock(&left_page_lk);
    left_page_num--;
    _release_spinlock(&left_page_lk);

    return page;
}

void kfree_page(void* p) {
    // _decrement_rc(&alloc_page_cnt);
    if(_decrement_rc(&pages_info[K2P(p)/PAGE_SIZE].ref)) {
        add_to_queue(&pages, (QueueNode*)p);

        _acquire_spinlock(&left_page_lk);
        left_page_num++;
        _release_spinlock(&left_page_lk);
    }
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

    slab_t* target_slab = (slab_t*)((u64)p >> 12 << 12);
    _insert_into_list(&target_slab -> objs, (ListNode*)p);
    target_slab -> used--;

    _detach_from_list(&target_slab -> ptNode);

    if (target_slab -> used == 0) {
        kfree_page((void*)target_slab);
    }
    else {
        _insert_into_list(&target_slab -> parent -> slabs_partial, &target_slab -> ptNode);
    }
    _release_spinlock(&mem_lock);
}

u64 left_page_cnt() {
    u64 left_cnt;
    _acquire_spinlock(&left_page_lk);
    left_cnt = left_page_num;
    _release_spinlock(&left_page_lk);

    return left_cnt;
    
}

void* get_zero_page() {
    void* zero_page = (void*)(PAGE_BASE((u64)&end) + PAGE_SIZE);
    return zero_page;
}

bool check_zero_page() {
    u8* zero_page = (u8*) get_zero_page();
    for (int i = 0; i < PAGE_SIZE; i++) {
        if (*(zero_page + i) != 0) {
            return false;
        }
    }
    return true;
}

void page_ref_plus(void* page) {
    _increment_rc(&pages_info[K2P(page)/PAGE_SIZE].ref);
}

u32 write_page_to_disk(void* ka) {
    // printk("----\n");
    u32 bno = find_and_set_8_blocks();
    // printk("%d\n", bno);
    // OpContext ctx;
    // bcache.begin_op(&ctx);
    for (usize i = 0; i < BLOCKS_PER_PAGE; i++) {
        // printk("----\n");
        Block* b = bcache.acquire(bno + i);
        memcpy(b->data, (u8*)ka + i*BLOCK_SIZE, BLOCK_SIZE);
        // printk("--b->data--:%lld\n", *(i64*)b->data);
        bcache.sync(NULL, b);
        bcache.release(b);
    }
    // bcache.end_op(&ctx);
    return bno;
}

void read_page_from_disk(void* ka, u32 bno) {
    // OpContext ctx;
    // bcache.begin_op(&ctx);
    
    for (usize i = 0; i < BLOCKS_PER_PAGE; i++) {
        Block* b = bcache.acquire(bno + i);
        memcpy((u8*)ka + i*BLOCK_SIZE, b->data, BLOCK_SIZE);
        // printk("b->data:%lld\n", *(i64*)b->data);
        // bcache.sync(&ctx, b);
        bcache.release(b);
    }
    // bcache.end_op(&ctx);

    release_8_blocks(bno);
}