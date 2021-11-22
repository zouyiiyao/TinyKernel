#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "global.h"

#define KDB_BUF_PORT 0x60    // 键盘buffer寄存器（8位）端口号为0x60

/* 键盘中断处理程序*/
static void intr_keyboard_handler(void) {
	// 必须要读取输出缓冲区寄存器，否则8042不会再往此寄存器存入新的扫描码
	uint8_t scancode = inb(KDB_BUF_PORT);
	put_int(scancode);
	return;
}

/* 键盘初始化 */
void keyboard_init(void) {
	put_str("keyboard init start\n");
	register_handler(0x21, intr_keyboard_handler);
	put_str("keyboard init done\n");
}
