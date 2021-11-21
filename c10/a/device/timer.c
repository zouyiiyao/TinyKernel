#include "timer.h"
#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "thread.h"
#include "debug.h"

/* 8253计数器向8259A中断控制器主片IRQ0引脚发送时钟中断的频率 */
#define IRQ0_FREQUENCY 100
/* 8253计数器CLK0引脚输入脉冲的频率，即1秒内会将计数器减去这么多次，计数器减到0则通过OUT引脚发出时钟中断信号 */
#define INPUT_FREQUENCY 1193180
/* 计数器0的计数初始值 */
#define COUNTER0_VALUE INPUT_FREQUENCY / IRQ0_FREQUENCY
/* 计数器0的端口号，往该端口写入计数器初始值 */
#define COUNTER0_PORT 0x40
/* 使用计数器0 */
#define COUNTER0_NO 0
/* 使用2工作模式，循环计数，用来实现对时钟脉冲CLK的N分频 */
#define COUNTER_MODE 2
/* 读写方式为先读写低字节，再读写高字节 */
#define READ_WRITE_LATCH 3
/* 8253计数器控制端口 */
#define PIT_CONTROL_PORT 0x43

/* ticks是内核自中断开启以来总共的滴答数 */
uint32_t ticks;

/* 把操作的计数器counter_no，读写锁属性rwl，计数器模式counter_mode写入模式控制寄存器，往计数器0的端口号写入计数初值 */
static void frequency_set(uint8_t counter_port, \
                          uint8_t counter_no, \
                          uint8_t rwl, \
                          uint8_t counter_mode, \
                          uint16_t counter_value) {
	/* 往控制字寄存器端口0x43写入控制字 */
	outb(PIT_CONTROL_PORT, (counter_no << 6 | rwl << 4 | counter_mode << 1));
	/* 先写入counter_value的低8位 */
	outb(counter_port, (uint8_t)counter_value);
	/* 再写入counter_value的高8位 */
	outb(counter_port, (uint8_t)counter_value >> 8);
}

/* 时钟的中断处理函数 */
static void intr_timer_handler(void) {
	struct task_struct* cur_thread = running_thread();    // 从内核栈时寄存器esp中取整，得到PBC地址
	
	ASSERT(cur_thread->stack_magic == 0x19870916);        // 检查栈是否溢出

	cur_thread->elapsed_ticks++;                          // 记录线程总共占用的CPU时钟滴答数
	ticks++;                                              // 从内核第一次处理时钟中断至今内核态和用户态总共的滴答数

	// 若当前线程时间片用完，就开始调度新的线程上CPU执行
	if (cur_thread->ticks == 0) {
		schedule();
	// 若当前线程时间片没有用完，则将当前线程时间片减1
	} else {
		cur_thread->ticks--;
	}
}

/* 初始化可编程中断定时/计数器（PIT 8253）*/
void timer_init() {
	put_str("timer_init start\n");
	// 设置8253的定时周期，也就是发中断的周期 */
	frequency_set(COUNTER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
	// 注册时钟中断处理程序
	register_handler(0x20, intr_timer_handler);
	put_str("timer_init done\n");
}
