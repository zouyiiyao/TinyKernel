#include "print.h"
#include "init.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"

int main(void) {
	put_str("I am kernel\n");
	init_all();

	intr_enable();                       // 打开中断，除了键盘中断，其他中断都被屏蔽

	while(1);

	return 0;
}
