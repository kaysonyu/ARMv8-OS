#include <aarch64/intrinsic.h>
#include <kernel/init.h>
#include <driver/uart.h>
#include <common/string.h>

static char hello[16];

extern char edata[], end[];

define_early_init(hello) {
    strncpy(hello, "Hello world!", 16);
}

define_init(print) {
    for (char* p = hello; *p; p++) {
        uart_put_char(*p);
    }
}

NO_RETURN void main()
{   
    if (cpuid() == 0) {
        for (char* p = &edata; p < &end; p++) {
            *p = 0;
        }
        do_early_init();
        do_init();
    }
    arch_stop_cpu();
}
