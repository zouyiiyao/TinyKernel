#include "print.h"
#include "init.h"
#include "memory.h"
#include "thread.h"

void k_thread_a(void*);

int main(void) {
	put_str("I am kernel\n");
	init_all();

	thread_start("k_thread_a", 31, k_thread_a, "argA ");

	// 之后的代码不可能执行到，因为执行流被转向kernel_thread

	while(1);
	return 0;
}

void k_thread_a(void* arg) {
	// 用void*来通用表示参数，被调用的函数知道自己需要什么类型的参数，自己转换后再用
	char* para = arg;
	while (1) {
		put_str(para);
	}
}
