#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "../device/timer.h"    // 用相对路径演示头文件包含
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall_init.h"
#include "ide.h"
#include "fs.h"

/* 负责初始化所有模块 */
void init_all() {
	put_str("init_all\n");
	idt_init();         // 初始化中断
	timer_init();       // 初始化PIT，含时钟中断处理程序注册
	mem_init();         // 初始化内存管理系统
	thread_init();      // 初始化线程
	console_init();     // 初始化控制台，最好在开中断前
	keyboard_init();    // 初始化键盘
	tss_init();         // 初始化tss
	syscall_init();     // 初始化系统调用

	intr_enable();      // ide_init需要打开中断
	ide_init();         // 初始化硬盘
	filesys_init();     // 初始化文件系统
}
