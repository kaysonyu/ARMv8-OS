#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

PTEntriesPtr get_pte(struct pgdir* pgdir, u64 va, bool alloc)
{
    // TODO
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.

    PTEntriesPtr pt0, pt1, pt2, pt3;

    pt0 = pgdir -> pt;
    if (!pt0 && !alloc) return NULL;
    if (!pt0) {
        pt0 = kalloc_page();
        memset(pt0, 0, PAGE_SIZE); 
    }

    pt1 = (PTEntriesPtr)pt0[VA_PART0(va)];
    if (!pt1 && !alloc) return NULL;
    if (!pt1) {
        pt1 = kalloc_page();
        memset(pt1, 0, PAGE_SIZE); 
    }

    pt2 = (PTEntriesPtr)pt1[VA_PART1(va)];
    if (!pt2 && !alloc) return NULL;
    if (!pt2) {
        pt2 = kalloc_page();
        memset(pt2, 0, PAGE_SIZE); 
    }

    pt3 = (PTEntriesPtr)pt2[VA_PART2(va)];
    if (!pt3 && !alloc) return NULL;
    if (!pt3) {
        pt3 = kalloc_page();
        memset(pt3, 0, PAGE_SIZE); 
    }

    return &pt3[VA_PART3(va)];
}

void init_pgdir(struct pgdir* pgdir)
{
    pgdir->pt = NULL;
}

void traverse_free(PTEntriesPtr table, u32 traverse_n) {
    if (--traverse_n) {
        for (u32 i = 0; i < N_PTE_PER_TABLE; i++) {
            if (table[i]) {
                traverse_free((PTEntriesPtr)table[i], traverse_n);
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
    PTEntriesPtr pt0 = pgdir -> pt;
    if (pt0) {
        traverse_free(pt0, 4);
    }
}

void attach_pgdir(struct pgdir* pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}



