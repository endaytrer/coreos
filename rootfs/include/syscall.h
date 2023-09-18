#ifndef __SYSCALL_H
#define __SYSCALL_H
#include <stdint.h>


#define SYS_WRITE 64
#define SYS_EXIT 93

static inline __attribute__((always_inline)) int64_t syscall(uint64_t id, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    register uint64_t x10 asm("x10") = arg0;
    register uint64_t x11 asm("x11") = arg1;
    register uint64_t x12 asm("x12") = arg2;
    register uint64_t x17 asm("x17") = id;
    asm volatile(
        "ecall\n"
        : "+r" (x10)
        : "r"(x10), "r" (x11), "r" (x12), "r" (x17)
    );
    return x10;
}

int64_t sys_write(uint64_t fd, const char *buffer);
int64_t sys_exit(int32_t xstate);


#endif