#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>

#define PAGE_NUM PHYSTOP/PAGE_SIZE
//for test
// #define PAGE_NUM 2000
#define ALLOC_SIZE 10

SpinLock mem_lock;

RefCount alloc_page_cnt;

define_early_init(alloc_page_cnt)
{
    init_rc(&alloc_page_cnt);
}

static QueueNode* pages;
QueueNode* page_manage [PAGE_NUM][ALLOC_SIZE];
extern char end[];
define_early_init(pages)
{   
    for (u64 i = 0; i < PAGE_NUM; i++)
        for (u64 j = 0; j < ALLOC_SIZE; j++)
            page_manage[i][j] = NULL;

    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE) {
        add_to_queue(&pages, (QueueNode*)p);  
    }        
}

int log2_8(isize size) {
    int log_result;
    for (log_result = 0; log_result < 13 && (1 << log_result) < size; log_result++) {}
    return log_result > 3 ? log_result - 3 : 0;
}

void* kalloc_page()
{
    _increment_rc(&alloc_page_cnt);
    return fetch_from_queue(&pages);
}

void kfree_page(void* p)
{
    _decrement_rc(&alloc_page_cnt);
    add_to_queue(&pages, (QueueNode*)p);
}

void* kalloc(isize size)
{
    _acquire_spinlock(&mem_lock);
    int size_num = log2_8(size+8);
    static int page_count = 0;
    bool flag = false;
    int page_order, size_order;
    for (int i = 0; i < PAGE_NUM && !flag; i++) {
        for (int j = size_num; j < ALLOC_SIZE && !flag; j++) {
            if (page_manage[i][j] != NULL) {
                page_order = i;
                size_order = j;
                flag = true;
            }
        }
    }
    if (!flag) {
        add_to_queue(&page_manage[page_count][ALLOC_SIZE-1], kalloc_page());
        page_order = page_count;
        size_order = ALLOC_SIZE-1;
        flag = true;
        page_count++;
    }
   
    u64 p = (u64)fetch_from_queue(&page_manage[page_order][size_order]);
    for (int i = size_order-1; i >= size_num; i--) {
        add_to_queue(&page_manage[page_order][i], (QueueNode*)(p+(1<<i)*8));
    }
    ((int*)p)[0] = page_order;
    ((int*)p)[1] = size_num;
    _release_spinlock(&mem_lock);
    return (QueueNode*)(p+8);

}

void kfree(void* p)
{   
    _acquire_spinlock(&mem_lock);
    int* int_p = ((int*)p) - 2;
    add_to_queue(&page_manage[int_p[0]][int_p[1]], (QueueNode*)int_p);
    _release_spinlock(&mem_lock);
}
