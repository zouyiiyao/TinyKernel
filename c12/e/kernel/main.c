#include "print.h"
#include "init.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall_init.h"
#include "syscall.h"
#include "stdio.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
int prog_a_pid = 0, prog_b_pid = 0;

int main(void) {
	put_str("I am kernel\n");
	init_all();

	intr_enable();                       // 打开中断，除了时钟中断，其他中断都被屏蔽
	
	thread_start("k_thread_a", 31, k_thread_a, "argA ");
	thread_start("k_thread_b", 31, k_thread_b, "argB ");
	while(1);
	return 0;
}

/* 在线程中运行的函数 */
void k_thread_a(void* arg) {
	char* para = arg;
	void* addr = sys_malloc(33);
	console_put_str("I am thread_a, sys_malloc(33), addr is 0x");
	console_put_int((int)addr);
	console_put_char('\n');
	while (1);
}

/* 在线程中运行的函数 */
void k_thread_b(void* arg) {
	char* para = arg;
	void* addr = sys_malloc(63);
	console_put_str("I am thread_b, sys_malloc(63), addr is 0x");
	console_put_int((int)addr);
	console_put_char('\n');
	while (1);

}

/* 测试用户进程 */
void u_prog_a(void) {
	char* name = "prog_a";
	prog_a_pid = getpid();
	printf("I am %s, my pid:%d%c", name, prog_a_pid, '\n');
	while (1);
}

/* 测试用户进程 */
void u_prog_b(void) {
	char* name = "prog_b";
	prog_b_pid = getpid();
	printf("I am %s, my pid:%d%c", name, prog_b_pid, '\n');
	while (1);
}
