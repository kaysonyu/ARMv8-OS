#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <kernel/pt.h>
#include <kernel/paging.h>

void* syscall_table[NR_SYSCALL];

void syscall_entry(UserContext* context)
{
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    u64 id = context -> x[8];
    if (id < NR_SYSCALL) {
        if (syscall_table[id] == NULL) PANIC();

        context -> x[0] = (*(u64(*)(u64, u64, u64, u64, u64, u64))(syscall_table[id]))(context -> x[0], context -> x[1], context -> x[2], context -> x[3], context -> x[4], context -> x[5]);
    }
}

// check if the virtual address [start,start+size) is READABLE by the current user process
bool user_readable(const void* start, usize size) {
    u64 va = (u64)start;
    u64 va_base;
    u64 n;
    while (size > 0) {
        va_base = PAGE_BASE(va);
        PTEntriesPtr pte = get_pte(&thisproc()->pgdir, va_base, false);
        if (pte == NULL) {
            return false;
        }
        if (!(*pte & PTE_USER_DATA)) {
            return false;
        }
        n = PAGE_SIZE - (va - va_base);
        if (n > size) {
            n = size;
        }
        size -= n;
        va = va_base + PAGE_SIZE;
    }
    return true;
}

// check if the virtual address [start,start+size) is READABLE & WRITEABLE by the current user process
bool user_writeable(const void* start, usize size) {
    u64 va = (u64)start;
    u64 va_base;
    u64 n;
    while (size > 0) {
        va_base = PAGE_BASE(va);
        PTEntriesPtr pte = get_pte(&thisproc()->pgdir, va_base, false);
        if (pte == NULL) {
            return false;
        }
        if (!(*pte & PTE_USER_DATA) || !(*pte & PTE_RO)) {
            return false;
        }
        n = PAGE_SIZE - (va - va_base);
        if (n > size) {
            n = size;
        }
        size -= n;
        va = va_base + PAGE_SIZE;
    }
    return true;
}

// get the length of a string including tailing '\0' in the memory space of current user process
// return 0 if the length exceeds maxlen or the string is not readable by the current user process
usize user_strlen(const char* str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}
