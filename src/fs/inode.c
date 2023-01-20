#include <common/string.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <kernel/console.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <sys/stat.h>
#include <kernel/proc.h>
#include <kernel/sched.h>

// this lock mainly prevents concurrent access to inode list `head`, reference
// count increment and decrement.
static SpinLock lock;
static ListNode head;

static const SuperBlock* sblock;
static const BlockCache* cache;

// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);

    for(u32 i_no = 1; i_no < sblock->num_inodes; i_no++){
        Block* block = cache->acquire(sblock->inode_start + i_no/IPB);
        InodeEntry* d_inode = (InodeEntry*)block->data + i_no%IPB;
        if (d_inode->type == INODE_INVALID) { 
            memset(d_inode, 0, sizeof(InodeEntry));
            d_inode->type = type;
            cache->sync(ctx, block);  
            cache->release(block);
            return i_no;
        }
        cache->release(block);
    }
    PANIC();
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    unalertable_wait_sem(&inode->lock);
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    _post_sem(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    usize inode_no = inode->inode_no;
    Block* block = cache->acquire(sblock->inode_start + inode_no/IPB);
    InodeEntry* d_inode = (InodeEntry*)block->data + inode_no%IPB;

    if (inode->valid && do_write) {
        memcpy(d_inode, &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, block);
        cache->release(block);
    }
    else if (!inode->valid) {
        memcpy(&inode->entry, d_inode, sizeof(InodeEntry));
        cache->release(block);
        inode->valid = true;
    }
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);

    _acquire_spinlock(&lock);
    
    _for_in_list(node, &head) {
        if (node == &head)  continue;

        Inode* inode = container_of(node, Inode, node);
        if (inode->inode_no == inode_no){
            _increment_rc(&inode->rc);
            _release_spinlock(&lock);
            return inode;
        }
    }

    Block* block = cache->acquire(sblock->inode_start + inode_no/IPB);
    InodeEntry* d_inode = (InodeEntry*)block->data + inode_no%IPB;
    ASSERT(d_inode->type != INODE_INVALID);

    Inode* inode_new = kalloc(sizeof(Inode));
    init_inode(inode_new);
    _increment_rc(&inode_new->rc);
    inode_new->inode_no = inode_no;
    memcpy(&inode_new->entry, d_inode, sizeof(InodeEntry));
    inode_new->valid = true;
    _insert_into_list(&head, &inode_new->node);
    
    cache->release(block);

    _release_spinlock(&lock);
    return inode_new;
}

// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    for (usize i = 0; i < INODE_NUM_DIRECT; i++) {
        if (inode->entry.addrs[i] != 0) {
            cache->free(ctx, inode->entry.addrs[i]);
            inode->entry.addrs[i] = 0;
        }
    }
    if (inode->entry.indirect) {
        Block* indirect_block = cache->acquire(inode->entry.indirect);
        IndirectBlock* indirect = (IndirectBlock*)indirect_block->data;
        for (usize i = 0; i < INODE_NUM_INDIRECT; i++) {
            if (indirect->addrs[i] != 0) {
                cache->free(ctx, indirect->addrs[i]);
            }
        }
        cache->release(indirect_block); 

        cache->free(ctx, inode->entry.indirect);
        inode->entry.indirect = 0;
    }
    inode->entry.num_bytes = 0;
    inode_sync(ctx, inode, true);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    _acquire_spinlock(&lock);
    _increment_rc(&inode->rc);
    _release_spinlock(&lock);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    _acquire_spinlock(&lock);

    _decrement_rc(&inode->rc);

    if (inode->rc.count > 0) {
        _release_spinlock(&lock);
        return;
    }
    
    if ((inode->entry.num_links == 0)) {
        unalertable_wait_sem(&inode->lock);
        _release_spinlock(&lock);

        inode_clear(ctx, inode);

        usize i_no = inode->inode_no;
        Block* block = cache->acquire(sblock->inode_start + i_no/IPB);
        // printk("---IPB:%d, i_no:%d, i_noIPB:%d---\n", IPB, i_no, i_no%IPB);
        InodeEntry* d_inode = (InodeEntry*)block->data + i_no%IPB;
        memset(d_inode, 0, sizeof(InodeEntry));
        cache->sync(ctx, block);  
        cache->release(block);

        post_sem(&inode->lock);
        _acquire_spinlock(&lock);
    }
    _detach_from_list(&inode->node);
    kfree(inode);
    _release_spinlock(&lock);
}

// this function is private to inode layer, because it can allocate block
// at arbitrary offset, which breaks the usual file abstraction.
//
// retrieve the block in `inode` where offset lives. If the block is not
// allocated, `inode_map` will allocate a new block and update `inode`, at
// which time, `*modified` will be set to true.
// the block number is returned.
//
// NOTE: caller must hold the lock of `inode`.
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    if (modified)   *modified = false;
    usize off_no = offset / BLOCK_SIZE;
    if (off_no < INODE_NUM_DIRECT) {
        if (inode->entry.addrs[off_no] == 0) {
            inode->entry.addrs[off_no] = cache->alloc(ctx);
            if (modified)   *modified = true;
        }
        return inode->entry.addrs[off_no];
    }
    off_no -= INODE_NUM_DIRECT;
    ASSERT(off_no < INODE_NUM_INDIRECT);

    usize indirect_no = inode->entry.indirect;
    if(indirect_no == 0) {
        inode->entry.indirect = indirect_no = cache->alloc(ctx);
    }
    Block* indirect_block = cache->acquire(indirect_no);
    IndirectBlock* indirect = (IndirectBlock*)indirect_block->data;
    if (indirect->addrs[off_no] == 0) {
        indirect->addrs[off_no] = cache->alloc(ctx);
        if (modified)   *modified = true;
    }
    usize bno = indirect->addrs[off_no];
    cache->sync(ctx, indirect_block);
    cache->release(indirect_block);
    return bno;
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (inode->entry.type == INODE_DEVICE) {
        ASSERT(inode->entry.major == 1);
        return console_read(inode, (char*)dest, count);
    }
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    usize cnt;
    usize inc;
    for (cnt = 0; cnt < count; cnt += inc, offset += inc, dest += inc) {
        bool modified;
        usize bno = inode_map(NULL, inode, offset, &modified);
        ASSERT(!modified);

        Block* block = cache->acquire(bno);
        usize inc1 = BSIZE - offset % BSIZE;
        usize inc2 = count - cnt;
        inc = (inc1 < inc2) ? inc1 : inc2;
        memcpy(dest, block->data + offset % BSIZE, inc);
        cache->release(block);
    }

    return cnt;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    if (inode->entry.type == INODE_DEVICE) {
        ASSERT(inode->entry.major == 1);
        return console_write(inode, (char*)src, count);
    }
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    usize cnt;
    usize inc;
    for (cnt = 0; cnt < count; cnt += inc, offset += inc, src += inc) {
        bool modified;
        usize bno = inode_map(ctx, inode, offset, &modified);

        Block* block = cache->acquire(bno);
        usize inc1 = BSIZE - offset % BSIZE;
        usize inc2 = count - cnt;
        inc = (inc1 < inc2) ? inc1 : inc2;
        memcpy(block->data + offset%BSIZE, src, inc);
        cache->sync(ctx, block);
        cache->release(block);
    }

    if (end > inode->entry.num_bytes)
        inode->entry.num_bytes = end;
    inode_sync(ctx, inode, true);

    return cnt;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    DirEntry d_entry;
    for(usize offset = 0; offset < entry->num_bytes; offset += sizeof(DirEntry)) {
        usize cnt = inode_read(inode, (u8*)&d_entry, offset, sizeof(DirEntry));
        ASSERT(cnt == sizeof(DirEntry));

        if (d_entry.inode_no == 0) {
            continue;
        }
        // printk("**%s**%s**%d\n", d_entry.name, name, memcmp(name, d_entry.name, FILE_NAME_MAX_LENGTH));
        if (strncmp(name, d_entry.name, FILE_NAME_MAX_LENGTH) == 0) {
            if (index) {
                *index = offset; 
            }
            return d_entry.inode_no;
        }
    }

    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    if (inode_lookup(inode, name, NULL) != 0) {
        return -1;
    }

    DirEntry d_entry;
    usize offset;
    for(offset = 0; offset < entry->num_bytes; offset += sizeof(DirEntry)) {
        usize cnt = inode_read(inode, (u8*)&d_entry, offset, sizeof(DirEntry));
        ASSERT(cnt == sizeof(DirEntry));

        if (d_entry.inode_no == 0) {
            break;
        }
    }
    // printk("--%d--%s\n", offset, name);
    d_entry.inode_no = inode_no;
    strncpy(d_entry.name, name, FILE_NAME_MAX_LENGTH);
    usize cnt = inode_write(ctx, inode, (u8*)&d_entry, offset, sizeof(DirEntry));
    ASSERT(cnt == sizeof(DirEntry));

    return offset;
}



// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    // TODO
    DirEntry d_entry;
    usize cnt = inode_read(inode, (u8*)&d_entry, index, sizeof(DirEntry));
    ASSERT(cnt == sizeof(DirEntry));

    if (d_entry.inode_no != 0) {
        d_entry.inode_no = 0;
        memset(d_entry.name, 0, FILE_NAME_MAX_LENGTH);
        usize cnt = inode_write(ctx, inode, (u8*)&d_entry, index, sizeof(DirEntry));
        ASSERT(cnt == sizeof(DirEntry));
    }
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};
/* Paths. */

/* Copy the next path element from path into name.
 *
 * Return a pointer to the element following the copied one.
 * The returned path has no leading slashes,
 * so the caller can check *path=='\0' to see if the name is the last one.
 * If no name to remove, return 0.
 *
 * Examples:
 *   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
 *   skipelem("///a//bb", name) = "bb", setting name = "a"
 *   skipelem("a", name) = "", setting name = "a"
 *   skipelem("", name) = skipelem("////", name) = 0
 */
static const char* skipelem(const char* path, char* name) {
    const char* s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= FILE_NAME_MAX_LENGTH)
        memmove(name, s, FILE_NAME_MAX_LENGTH);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

/* Look up and return the inode for a path name.
 *
 * If parent != 0, return the inode for the parent and copy the final
 * path element into name, which must have room for DIRSIZ bytes.
 * Must be called inside a transaction since it calls iput().
 */
static Inode* namex(const char* path,
                    int nameiparent,
                    char* name,
                    OpContext* ctx) {
    Inode *target_inode, *next_inode;
    usize next_ino;

    if (*path == '/') {
        target_inode = inode_get(ROOT_INODE_NO);
    }
    else {
        target_inode = inode_share(thisproc()->cwd);
    }
        
    while ((path = skipelem(path, name)) != NULL) {
        inode_lock(target_inode);

        //保证当前级为目录
        if (target_inode->entry.type != INODE_DIRECTORY) {
            inode_unlock(target_inode);
            inode_put(ctx, target_inode);
            return NULL;
        }

        // /a/b/c taget_inode为b, 且返回父级目录
        if (nameiparent && *path == '\0') {
            inode_unlock(target_inode);
            return target_inode;
        }

        //利用name查找下一级
        next_ino = inode_lookup(target_inode, name, NULL);
        if (next_ino == 0) {
            inode_unlock(target_inode);
            inode_put(ctx, target_inode);
            return NULL;
        }
        next_inode = inode_get(next_ino);
        inode_unlock(target_inode);
        inode_put(ctx, target_inode);
        target_inode = next_inode;
    }

    if (nameiparent) {
        inode_put(ctx, target_inode);
        return NULL;
    }

    return target_inode;
}

Inode* namei(const char* path, OpContext* ctx) {
    char name[FILE_NAME_MAX_LENGTH];
    return namex(path, 0, name, ctx);
}

Inode* nameiparent(const char* path, char* name, OpContext* ctx) {
    return namex(path, 1, name, ctx);
}

/*
 * Copy stat information from inode.
 * Caller must hold ip->lock.
 */
void stati(Inode* ip, struct stat* st) {
    st->st_dev = 1;
    st->st_ino = ip->inode_no;
    st->st_nlink = ip->entry.num_links;
    st->st_size = ip->entry.num_bytes;
    switch (ip->entry.type) {
        case INODE_REGULAR:
            st->st_mode = S_IFREG;
            break;
        case INODE_DIRECTORY:
            st->st_mode = S_IFDIR;
            break;
        case INODE_DEVICE:
            st->st_mode = 0;
            break;
        default:
            PANIC();
    }
}
