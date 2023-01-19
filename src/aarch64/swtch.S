// Do kernel-mode context switch
// x0 (first parameter): new context ptr
// x1 (second parameter): addr to save old context ptr

/*
typedef struct KernelContext
{
    u64 lr, x0, x1;
    u64 x[11];  //x19-x29
} KernelContext;
*/

//寄存器 a 和 b 的值存储到栈顶地址减去 0x10 字节处，并且将栈指针减去 0x10 个字节。
#define pushp(a, b) stp a, b, [sp, #-0x10]!
//从栈顶地址加载 a 和 b 的值，并且将栈指针增加 0x10 个字节
#define popp(a, b) ldp a, b, [sp], #0x10 

.globl swtch
swtch:
// TODO
pushp(x28, x29)
pushp(x26, x27)
pushp(x24, x25)
pushp(x22, x23)
pushp(x20, x21)
pushp(x1, x19)
pushp(lr, x0)
mov x2, sp
str x2, [x1]
mov sp, x0
popp(lr, x0)
popp(x1, x19)
popp(x20, x21)
popp(x22, x23)
popp(x24, x25)
popp(x26, x27)
popp(x28, x29)
ret

