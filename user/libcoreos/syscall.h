#ifndef __SYSCALL_H
#define __SYSCALL_H
#include <type.h>


#define SYS_CHDIR    49
#define SYS_OPENAT   56
#define SYS_CLOSE    57
#define SYS_LSEEK    62
#define SYS_READ     63
#define SYS_WRITE    64
#define SYS_EXIT     93
#define SYS_YIELD    124
#define SYS_GET_TIME 169
#define SYS_SBRK     214
#define SYS_FORK     220
#define SYS_EXEC     221
#define SYS_WAITPID  260

#define SYSCALL static inline __attribute__((always_inline)) long

SYSCALL syscall(u64 id, u64 arg0, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6) {
    register u64 x10 asm("x10") = arg0;
    register u64 x11 asm("x11") = arg1;
    register u64 x12 asm("x12") = arg2;
    register u64 x13 asm("x13") = arg3;
    register u64 x14 asm("x14") = arg4;
    register u64 x15 asm("x15") = arg5;
    register u64 x16 asm("x16") = arg6;
    register u64 x17 asm("x17") = id;
    asm volatile("ecall"
    : "=r"(x10)
    : "r"(x10),"r"(x11),"r"(x12),"r"(x13),"r"(x14),"r"(x15),"r"(x16),"r"(x17)
    );
    return x10;
}


SYSCALL sys_chdir(const char *filename) {
    return syscall(SYS_CHDIR, (u64)filename, 0, 0, 0, 0, 0, 0);
}
SYSCALL sys_openat(i32 dfd, const char *filename, int flags, int mode) {
    return syscall(SYS_OPENAT, dfd, (u64)filename, flags, mode, 0, 0, 0);
}
SYSCALL sys_close(u32 fd) {
    return syscall(SYS_CLOSE, fd, 0, 0, 0, 0, 0, 0);
}
SYSCALL sys_lseek(u32 fd, i64 off_high, i64 off_low, u64 *result, u32 whence) {
    return syscall(SYS_LSEEK, fd, off_high, off_low, (u64)result, whence, 0, 0);
}
SYSCALL sys_read(u64 fd, char *buffer, u64 size) {
    return syscall(SYS_READ, fd, (u64)buffer, size, 0, 0, 0, 0);
}
SYSCALL sys_write(u64 fd, const char *buffer, u64 size) {
    return syscall(SYS_WRITE, fd, (u64)buffer, size, 0, 0, 0, 0);
}
SYSCALL sys_exit(i32 xstate) {
    return syscall(SYS_EXIT, (u64) xstate, 0, 0, 0, 0, 0, 0);
}
SYSCALL sys_yield(void) {
    return syscall(SYS_YIELD, 0, 0, 0, 0, 0, 0, 0);
}
SYSCALL sys_get_time(void *ts, u64 _tz) {
    return syscall(SYS_GET_TIME, (u64)ts, _tz, 0, 0, 0, 0, 0);
}
SYSCALL sys_sbrk(i64 size) {
    return syscall(SYS_SBRK, (u64)size, 0, 0, 0, 0, 0, 0);
}
SYSCALL sys_fork(void) {
    return syscall(SYS_FORK, 0, 0, 0, 0, 0, 0, 0);
}
SYSCALL sys_exec(const char *path) {
    return syscall(SYS_EXEC, (u64)path, 0, 0, 0, 0, 0, 0);
}
SYSCALL sys_waitpid(int pid) {
    return syscall(SYS_WAITPID, pid, 0, 0, 0, 0, 0, 0);
}

#endif