#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

/*
typedef struct UserContext
{
    u64 spsr, elr, lr, sp_el0;
    u64 q0[2];
    u64 tpidr_el0;
    u64 x[31];
    
} UserContext;
*/

/* `exception_vector.S` send all traps here. */
.global trap_entry
trap_entry:
// TODO
pushp(x29, x30)
pushp(x27, x28)
pushp(x25, x26)
pushp(x23, x24)
pushp(x21, x22)
pushp(x19, x20)
pushp(x17, x18)
pushp(x15, x16)
pushp(x13, x14)
pushp(x11, x12)
pushp(x9, x10)
pushp(x7, x8)
pushp(x5, x6)
pushp(x3, x4)
pushp(x1, x2)
mrs x1, tpidr_el0
pushp(x1, x0)
str q0, [sp, #-0x10]!
mrs x0, sp_el0
pushp(x30, x0)
mrs x0, spsr_el1
mrs x1, elr_el1
pushp(x0, x1)


mov x0, sp
bl trap_global_handler


.global trap_return
trap_return:
// TODO
popp(x0, x1)
msr spsr_el1, x0
msr elr_el1, x1
popp(x30, x0)
msr sp_el0, x0
ldr q0, [sp], #0x10
popp(x1, x0)
msr tpidr_el0, x1
popp(x1, x2)
popp(x3, x4)
popp(x5, x6)
popp(x7, x8)
popp(x9, x10)
popp(x11, x12)
popp(x13, x14)
popp(x15, x16)
popp(x17, x18)
popp(x19, x20)
popp(x21, x22)
popp(x23, x24)
popp(x25, x26)
popp(x27, x28)
popp(x29, x30)
eret

