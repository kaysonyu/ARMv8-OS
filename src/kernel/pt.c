#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>
#include <kernel/paging.h>

PTEntriesPtr get_pte(struct pgdir* pgdir, u64 va, bool alloc)
{
    // TODO
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.

    PTEntriesPtr pt0, pt1, pt2, pt3;

    pt0 = pgdir -> pt;
    if (pt0 == NULL && !alloc) return NULL;
    if (pt0 == NULL) {
        pt0 = kalloc_page();
        memset(pt0, 0, PAGE_SIZE); 
        pgdir -> pt = pt0;
    }

    pt1 = (PTEntriesPtr)(P2K(PTE_ADDRESS(pt0[VA_PART0(va)])));
    if (!IS_VALID(pt0[VA_PART0(va)]) && !alloc) return NULL;
    if (!IS_VALID(pt0[VA_PART0(va)])) {
        pt1 = kalloc_page();
        memset(pt1, 0, PAGE_SIZE); 
        pt0[VA_PART0(va)] = K2P(pt1) | PTE_TABLE;
    }

    pt2 = (PTEntriesPtr)(P2K(PTE_ADDRESS(pt1[VA_PART1(va)])));
    if (!IS_VALID(pt1[VA_PART1(va)]) && !alloc) return NULL;
    if (!IS_VALID(pt1[VA_PART1(va)])) {
        pt2 = kalloc_page();
        memset(pt2, 0, PAGE_SIZE); 
        pt1[VA_PART1(va)] = K2P(pt2) | PTE_TABLE;
    }

    pt3 = (PTEntriesPtr)(P2K(PTE_ADDRESS(pt2[VA_PART2(va)])));
    if (!IS_VALID(pt2[VA_PART2(va)]) && !alloc) return NULL;
    if (!IS_VALID(pt2[VA_PART2(va)])) {
        pt3 = kalloc_page();
        memset(pt3, 0, PAGE_SIZE); 
        pt2[VA_PART2(va)] = K2P(pt3) | PTE_TABLE;
    }

    return &pt3[VA_PART3(va)];
}

void init_pgdir(struct pgdir* pgdir)
{
    pgdir->pt = kalloc_page();
    memset(pgdir->pt, 0, PAGE_SIZE); 
    init_spinlock(&pgdir->lock);
    init_list_node(&pgdir->section_head);
    init_sections(&pgdir->section_head);
    pgdir->online = false;
}

void traverse_free(PTEntriesPtr table, u32 traverse_n) {
    if (--traverse_n) {
        for (u32 i = 0; i < N_PTE_PER_TABLE; i++) {
            if (table[i]) {
                PTEntriesPtr p = (PTEntriesPtr)(P2K(PTE_ADDRESS(table[i])));
                traverse_free(p, traverse_n);
            }
        }
    }  
    kfree_page(table);
}

void free_pgdir(struct pgdir* pgdir)
{
    // TODO
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    free_sections(pgdir);
    PTEntriesPtr pt0 = pgdir -> pt;
    if (pt0 != NULL) {
        traverse_free(pt0, 4);
    }

}

void attach_pgdir(struct pgdir* pgdir)
{
    extern PTEntries invalid_pt;
    _acquire_spinlock(&thisproc()->pgdir.lock);
    thisproc()->pgdir.online = false;
    _release_spinlock(&thisproc()->pgdir.lock);
    
    if (pgdir->pt) 
        arch_set_ttbr0(K2P(pgdir->pt));
    
    else
        arch_set_ttbr0(K2P(&invalid_pt));
    
    _acquire_spinlock(&pgdir->lock);
    pgdir->online = true;
    _release_spinlock(&pgdir->lock);
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir* pd, void* va, void *p, usize len){
    // TODO
}

void vmmap(struct pgdir* pd, u64 va, void* ka, u64 flags) {
    *get_pte(pd, va, true) = K2P(ka) | flags;
}
