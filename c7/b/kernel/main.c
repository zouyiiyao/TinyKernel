#include "print.h"
#include "init.h"
int main(void) {
	put_str("I am kernel\n");
	init_all();
	asm volatile ("sti");    // 为演示中断处理，在此临时开中断，它将标志寄存器eflags中的IF位置1
	while(1);
	return 0;
}
