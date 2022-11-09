#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

static const SuperBlock* sblock;
static const BlockDevice* device;
Semaphore sem;

// static SpinLock lock;     // protects block cache.
// static ListNode head;     // the list of all allocated in-memory block.
// static LogHeader header;  // in-memory copy of log header block.

static struct LRUcache {
    SpinLock lock;
    u32 capacity;
    u32 size;
    ListNode head;
} LRUcache;

// hint: you may need some other variables. Just add them here.
static struct LOG {
    /* data */
  SpinLock lock;
  u32 outstanding; 
  bool committing;  
  LogHeader header;
  u32 real_use;
} log;

// read the content from disk.
static INLINE void device_read(Block* block) {
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block* block) {
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start, (u8*)&(log.header));
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start, (u8*)&(log.header));
}

static void init_LRUcache() {
    init_spinlock(&LRUcache.lock);
    LRUcache.capacity = EVICTION_THRESHOLD;
    LRUcache.size = 0;
    init_list_node(&LRUcache.head);
}

static void init_log() {
    init_spinlock(&log.lock);
    log.outstanding = 0;
    log.committing = false;
    log.header.num_blocks = 0;
    log.real_use = 0;
}

// initialize a block struct.
static void init_block(Block* block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    return LRUcache.size;
}

// see `cache.h`.
static Block* cache_acquire(usize block_no) {
    // printk("~~%d\n", block_no);
    // TODO
    _acquire_spinlock(&LRUcache.lock);

    //判断cache中是否存在
    _for_in_list(node, &LRUcache.head) {
        if (node == &LRUcache.head) {
            continue;
        }
        Block* b = container_of(node, Block, node);
        if (b->block_no == block_no) {

            b->acquired = true;
            _lock_sem(&b->lock);
            _release_spinlock(&LRUcache.lock);
            ASSERT(_wait_sem(&b->lock, false));

            _acquire_spinlock(&LRUcache.lock);
            b->acquired = true;

            _detach_from_list(node);
            _insert_into_list(&LRUcache.head, node);

            _release_spinlock(&LRUcache.lock);

            return b;
        }
    }

    //kalloc一个新的block
    Block* block = kalloc(sizeof(Block));
    init_block(block);
    _insert_into_list(&LRUcache.head, &block->node);
    LRUcache.size++;
    block->block_no = block_no;
    block->acquired = true;
    _lock_sem(&block->lock);
    _release_spinlock(&LRUcache.lock);
    ASSERT(_wait_sem(&block->lock, false));

    device_read(block);
    block->valid = true;

    _acquire_spinlock(&LRUcache.lock);
    if (LRUcache.size > LRUcache.capacity) {
        ListNode* replace_node = LRUcache.head.prev;
        if (!container_of(replace_node, Block, node)->pinned && !container_of(replace_node, Block, node)->acquired) {
            Block* replace_block = container_of(replace_node, Block, node);
        
            _detach_from_list(replace_node);
            LRUcache.size--;
            kfree(replace_block);
        }
    }

    _release_spinlock(&LRUcache.lock);
    return block;
}

// see `cache.h`.
static void cache_release(Block* block) {
    // TODO
    _acquire_spinlock(&LRUcache.lock);
    _lock_sem(&block->lock);
    if (_query_sem(&block->lock) >= 0) {
        block->acquired = false;
    };
    _post_sem(&block->lock);
    _unlock_sem(&block->lock);
    _release_spinlock(&LRUcache.lock);
}

// see `cache.h`.
static void cache_begin_op(OpContext* ctx) {
    // TODO
    while (1) {
        _acquire_spinlock(&log.lock);
        if (log.committing) {
            _lock_sem(&sem);
            _release_spinlock(&log.lock);
            ASSERT(_wait_sem(&sem, false));
        }
        else if ((log.real_use + OP_MAX_NUM_BLOCKS > sblock->num_log_blocks - 1) || (log.real_use + OP_MAX_NUM_BLOCKS > LOG_MAX_SIZE)) {
            _lock_sem(&sem);
            _release_spinlock(&log.lock);
            ASSERT(_wait_sem(&sem, false));
            // unalertable_wait_sem(&sem);
        }
        else {
            log.outstanding++;
            ctx->rm = OP_MAX_NUM_BLOCKS;
            log.real_use += OP_MAX_NUM_BLOCKS;
            _release_spinlock(&log.lock);
            break;
        }
    }
}

// see `cache.h`.
static void cache_sync(OpContext* ctx, Block* block) {
    // TODO
    if (ctx == NULL) {
        device_write(block);
    }
    else {
        _acquire_spinlock(&log.lock);
        _acquire_spinlock(&LRUcache.lock);
        block->pinned = true;
        usize i;
        for (i = 0; i < log.header.num_blocks; i++) {
            if (log.header.block_no[i] == block->block_no)   
                break;
        }
        log.header.block_no[i] = block->block_no;
        if (i == log.header.num_blocks) {
            if (ctx->rm == 0) {
                PANIC();
            }
            else {
                log.header.num_blocks++;
                ctx->rm--;
            }
        }
        _release_spinlock(&LRUcache.lock);
        _release_spinlock(&log.lock);
    }
}

// see `cache.h`.
static void cache_end_op(OpContext* ctx) {
    // TODO
    _acquire_spinlock(&log.lock);
    log.outstanding--;
    log.real_use -= ctx->rm;
        
    if(log.outstanding == 0) {
        log.committing = true;
    } 
    else {
        post_all_sem(&sem);
    }
    
    if (log.committing) {
        // _acquire_spinlock(&log.lock_2);
        //write_log
        for (usize i = 0; i < log.header.num_blocks; i++) {
            Block* from_b = cache_acquire(log.header.block_no[i]);
            Block* to_b = cache_acquire(sblock->log_start + 1 + i);
            memmove(to_b->data, from_b->data, BLOCK_SIZE);
            device_write(to_b);
            cache_release(from_b);
            cache_release(to_b);
        }
        
        write_header();

        //log -> sd
        for (usize i = 0; i < log.header.num_blocks; i++) {
            Block* from_b = cache_acquire(sblock->log_start + 1 + i);
            Block* to_b = cache_acquire(log.header.block_no[i]);
            memmove(to_b->data, from_b->data, BLOCK_SIZE);
            device_write(to_b);
            to_b->pinned = false;
            cache_release(from_b);
            cache_release(to_b);
        }
        
        log.header.num_blocks = 0;
        write_header();
        // _release_spinlock(&log.lock_2);

        log.committing = false;
        log.real_use = 0;
        post_all_sem(&sem);
    }
    _release_spinlock(&log.lock);
}

// initialize block cache.
void init_bcache(const SuperBlock* _sblock, const BlockDevice* _device) {
    sblock = _sblock;
    device = _device;

    // TODO
    init_LRUcache();
    init_log();
    init_sem(&sem, 0);

    _acquire_spinlock(&log.lock);
    read_header();
    if (log.header.num_blocks > 0) {
        for (usize i = 0; i < log.header.num_blocks; i++) {
            Block* from_b = cache_acquire(sblock->log_start + 1 + i);
            Block* to_b = cache_acquire(log.header.block_no[i]);
            memmove(to_b->data, from_b->data, BLOCK_SIZE);
            device_write(to_b);
            cache_release(from_b);
            cache_release(to_b);
        }
    }

    log.header.num_blocks = 0;
    write_header();
    _release_spinlock(&log.lock);
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static usize cache_alloc(OpContext* ctx) {
    // TODO
    for (u32 i = 0; i < sblock->num_blocks; i += BIT_PER_BLOCK) {
        Block* bp_b = cache_acquire(i / BIT_PER_BLOCK + sblock->bitmap_start);

        for (u32 j = 0; j < BIT_PER_BLOCK && i + j < sblock->num_blocks; j++) {
            u8 m = 1 << (j % 8);
            if (!(bp_b->data[j / 8] & m)) {
                bp_b->data[j / 8] |= m;
                cache_sync(ctx, bp_b);
                cache_release(bp_b);
                Block* target_b = cache_acquire(i + j);
                memset(target_b->data, 0, BLOCK_SIZE);
                cache_sync(ctx, target_b);
                cache_release(target_b);
                return (i + j);
            } 
        }
        cache_release(bp_b);
    }
    PANIC();
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static void cache_free(OpContext* ctx, usize block_no) {
    // TODO
    Block* bp_b = cache_acquire(block_no / BIT_PER_BLOCK + sblock->bitmap_start);
    u8 m = 1 << ((block_no % BIT_PER_BLOCK) % 8);
    if (!(bp_b->data[(block_no % BIT_PER_BLOCK) / 8] & m)) {
        PANIC();
    }
    bp_b->data[(block_no % BIT_PER_BLOCK) / 8] &= ~m;
    cache_sync(ctx, bp_b);
    cache_release(bp_b);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};
