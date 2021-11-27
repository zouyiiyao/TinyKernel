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
	void* addr1;
	void* addr2;
	void* addr3;
	void* addr4;
	void* addr5;
	void* addr6;
	void* addr7;
	console_put_str("thread_a start\n");
	int max = 50;
	while (max-- > 0) {
		int size = 128;
		addr1 = sys_malloc(size);
		size *= 2;
		addr2 = sys_malloc(size);
		size *= 2;
		addr3 = sys_malloc(size);
		sys_free(addr1);
		addr4 = sys_malloc(size);
		size *= 128;
		addr5 = sys_malloc(size);
		addr6 = sys_malloc(size);
		sys_free(addr5);
		size *= 2;
		addr7 = sys_malloc(size);
		sys_free(addr6);
		sys_free(addr7);
		sys_free(addr2);
		sys_free(addr3);
		sys_free(addr4);
	}
	console_put_str("thread_a end\n");
	while (1);
}

/* 在线程中运行的函数 */
void k_thread_b(void* arg) {
	char* para = arg;
	void* addr1;
	void* addr2;
	void* addr3;
	void* addr4;
	void* addr5;
	void* addr6;
	void* addr7;
	void* addr8;
	void* addr9;
	int max = 50;
	console_put_str("thread_b start\n");
	while (max-- > 0) {
		int size = 9;
		addr1 = sys_malloc(size);
		size *= 2;
		addr2 = sys_malloc(size);
		size *= 2;
		sys_free(addr2);
		addr3 = sys_malloc(size);
		sys_free(addr1);
		addr4 = sys_malloc(size);
		addr5 = sys_malloc(size);
		addr6 = sys_malloc(size);
		sys_free(addr5);
		size *= 2;
		addr7 = sys_malloc(size);
		sys_free(addr6);
		sys_free(addr7);
		sys_free(addr3);
		sys_free(addr4);

		size *= 8;
		addr1 = sys_malloc(size);
		addr2 = sys_malloc(size);
		addr3 = sys_malloc(size);
		addr4 = sys_malloc(size);
		addr5 = sys_malloc(size);
		addr6 = sys_malloc(size);
		addr7 = sys_malloc(size);
		addr8 = sys_malloc(size);
		addr9 = sys_malloc(size);
		sys_free(addr1);
		sys_free(addr2);
		sys_free(addr3);
		sys_free(addr4);
		sys_free(addr5);
		sys_free(addr6);
		sys_free(addr7);
		sys_free(addr8);
		sys_free(addr9);
	}
	console_put_str("thread_b end\n");
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
