#include "print.h"
#include "init.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"

void k_thread_a(void*);
void k_thread_b(void*);

int main(void) {
	put_str("I am kernel\n");
	init_all();

	thread_start("k_thread_a", 31, k_thread_a, "argA ");
	thread_start("k_thread_b", 8, k_thread_b, "argB ");

	intr_enable();                       // 打开时钟中断，开始调度执行

	while(1) {
		console_put_str("Main ");    // 先获得锁，再打印，之后释放锁
	}

	return 0;
}

void k_thread_a(void* arg) {
	// 用void*来通用表示参数，被调用的函数知道自己需要什么类型的参数，自己转换后再用
	char* para = arg;
	while (1) {
		console_put_str(para);    // 先获得锁，再打印，之后释放锁
	}
}

void k_thread_b(void* arg) {
	char* para = arg;
	while (1) {
		console_put_str(para);    // 先获得锁，再打印，之后释放锁
	}
}
