#include "syscall.h"
#include "thread.h"

/* 无参数的系统调用 */
#define _syscall0(NUMBER) ({ \
	int retval; \
	asm volatile ("int $0x80" : "=a" (retval) : "a" (NUMBER) : "memory"); \
	retval; \
})

/* 一个参数的系统调用 */
#define _syscall1(NUMBER, ARG1) ({ \
	int retval; \
	asm volatile ("int $0x80" : "=a" (retval) : "a" (NUMBER), "b" (ARG1) : "memory"); \
	retval; \
})

/* 两个参数的系统调用 */
#define _syscall2(NUMBER, ARG1, ARG2) ({ \
	int retval; \
	asm volatile ("int $0x80" : "=a" (retval) : "a" (NUMBER), "b" (ARG1), "c" (ARG2) : "memory"); \
	retval; \
})

/* 三个参数的系统调用 */
#define _syscall3(NUMBER, ARG1, ARG2, ARG3) ({ \
	int retval; \
	asm volatile ("int $0x80" : "=a" (retval) : "a" (NUMBER), "b" (ARG1), "c" (ARG2), "d" (ARG3) : "memory"); \
	retval; \
})

/* 返回当前任务的pid */
uint32_t getpid() {
	return _syscall0(SYS_GETPID);
}

/* write的功能目前是打印字符串str */
uint32_t write(int32_t fd, const void* buf, uint32_t count) {
	// return _syscall1(SYS_WRITE, str);
	return _syscall3(SYS_WRITE, fd, buf, count);
}

/* 申请size字节大小的内存，返回虚拟地址 */
void* malloc(uint32_t size) {
	return (void*)_syscall1(SYS_MALLOC, size);
}

/* 释放ptr指向的内存 */
void free(void* ptr) {
	_syscall1(SYS_FREE, ptr);
}

/* 派生子进程，返回子进程pid */
pid_t fork(void) {
	return _syscall0(SYS_FORK);
}

/* 从文件描述符fd中读取count个字节到buf */
int32_t read(int32_t fd, void* buf, uint32_t count) {
	return _syscall3(SYS_READ, fd, buf, count);
}

/* 输出一个字符 */
void putchar(char char_asci) {
	_syscall1(SYS_PUTCHAR, char_asci);
}

/* 清空屏幕 */
void clear(void) {
	_syscall0(SYS_CLEAR);
}

