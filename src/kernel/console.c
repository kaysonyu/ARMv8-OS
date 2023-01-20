#include<kernel/console.h>
#include<kernel/init.h>
#include<aarch64/intrinsic.h>
#include<kernel/sched.h>
#include<driver/uart.h>
#include<common/spinlock.h>
#include <driver/interrupt.h>
#include<kernel/printk.h>

#define INPUT_BUF 128
struct {
    SpinLock lock;
    SleepLock rlock;

    char buf[INPUT_BUF];
    usize r;  // Read index
    usize w;  // Write index
    usize e;  // Edit index
} input;
#define C(x)      ((x) - '@')  // Control-x

extern InodeTree inodes;

void console_intr_();

define_init(console) {
    input.r = 0;
    input.w = 0; 
    input.e = 0;
    init_spinlock(&input.lock);
    init_sem(&input.rlock, 0);
    set_interrupt_handler(IRQ_AUX, console_intr_);
}


isize console_write(Inode *ip, char *buf, isize n) {
    if (ip->entry.type != INODE_DEVICE) {
        return -1;
    }
    inodes.unlock(ip);

    _acquire_spinlock(&input.lock);
    for (int i = 0; i < n; i++) {
        uart_put_char(buf[i]);
    }
    _release_spinlock(&input.lock);

    inodes.lock(ip);
    return n;
}

//读取console缓冲区
isize console_read(Inode *ip, char *dst, isize n) {
    char c;
    isize target = n;

    inodes.unlock(ip);

    _acquire_spinlock(&input.lock);
    
    while (n > 0) {
        while (input.r == input.w) {
            if (thisproc()->killed) {
                _release_spinlock(&input.lock);
                inodes.lock(ip);
                return -1;
            }

            //sleep 获取所有信号量
            get_all_sem(&input.rlock);
            _lock_sem(&input.rlock);
            _release_spinlock(&input.lock);
            bool ret = _wait_sem(&input.rlock, false);
            ASSERT(ret || true);


            _acquire_spinlock(&input.lock);
        }
        c = input.buf[input.r++ % INPUT_BUF];
        if (c == C('D')) {
            if (n < target) {
                input.r--;
            }
            break;
        }
        *(dst++) = c;
        n--;
        if (c == '\n') {
            break;
        }  
    }
    
    _release_spinlock(&input.lock);

    inodes.lock(ip);
    return (target - n);
}

void console_intr(char (*getc)()) {
    _acquire_spinlock(&input.lock);
    
    while (1) {
        char c = getc();
        if (c == '\0')
            break;
        
        switch (c) {

            //删除前一个字符
            case '\b': {
                if (input.e > input.w) {
                    input.e--;
                    uart_put_char('\b'); 
                    uart_put_char(' '); 
                    uart_put_char('\b');
                }
                break;
            }

            //删除一行
            case C('U'): {
                while (input.e > input.w && input.buf[(input.e-1) % INPUT_BUF] != '\n') {
                    input.e--;
                    uart_put_char('\b'); 
                    uart_put_char(' '); 
                    uart_put_char('\b');
                }
                break;
            }

            //更新input.w到input.e
            case C('D'):
            case '\n': {
                if (input.e - input.r >= INPUT_BUF) {
                    break;
                }
                input.w = input.e;
                input.buf[input.e++ % INPUT_BUF] = '\n';
                uart_put_char('\n');
                
                post_all_sem(&input.rlock);
                break;
            }

            //杀死当前程序
            case C('C'): {
                int ret = kill(thisproc()->pid);
                ASSERT(ret || true);
                break;
            }

            //普通字符写入和回显
            default: {
                if (input.e - input.r < INPUT_BUF) {
                    input.buf[input.e++ % INPUT_BUF] = c;
                    uart_put_char(c);

                    if (input.e - input.r == INPUT_BUF) {
                        input.w = input.e;
                        post_all_sem(&input.rlock);
                    }
                }
                break;
            }
        }

    }
    

    _release_spinlock(&input.lock);
}

void console_intr_() {
    console_intr(uart_get_char);
}
