#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define PIC_M_CTRL 0x20      // 主片的控制端口是0x20
#define PIC_M_DATA 0x21      // 主片的数据端口是0x21
#define PIC_S_CTRL 0xa0      // 从片的控制端口是0xa0
#define PIC_S_DATA 0xa1      // 从片的数据端口是0xa1

#define IDT_DESC_CNT 0x30    // 目前总共支持的总中断数

/* 用于判断eflags寄存器的IF位是否为1（即开中断） */
#define EFLAGS_IF 0x00000200
/* pushfl指令先将eflags的值入栈，popl指令将eflags的值出栈，存入C变量EFLAG_VAR中 */
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl; popl %0" : "=g" (EFLAG_VAR))

/* 中断门描述符结构体 */
struct gate_desc {
	uint16_t func_offset_low_word;     // 中断处理程序在目标段内的偏移量的15-0位
	uint16_t selector;                 // 中断处理程序目标代码段描述符选择子
	uint8_t dcount;                    // 固定值，不用考虑
	uint8_t attribute;                 // 中断门描述符属性
	uint16_t func_offset_high_word;    // 中断处理程序在目标段内的偏移量的31-16位
};

/* 静态函数声明 */
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function);

/* idt是中断描述符表，本质上就是个中断门描述符数组 */
static struct gate_desc idt[IDT_DESC_CNT];

/* 用于保存异常的名字 */
char* intr_name[IDT_DESC_CNT];

/* 定义中断处理程序数组，在kernel.S中定义的intrXXentry只是中断处理程序的入口，最终调用的是ide_table中的处理程序 */
intr_handler idt_table[IDT_DESC_CNT];

/* 声明引用定义在kernel.S中的中断处理函数入口数组 */
extern intr_handler intr_entry_table[IDT_DESC_CNT];

/* 初始化可编程中断控制器8259A */
static void pic_init(void) {

	// 初始化主片
	outb(PIC_M_CTRL, 0x11);    // ICW1：边沿触发，级联8259，需要ICW4
	outb(PIC_M_DATA, 0x20);    // ICW2：起始中断向量号为0x20，对应的是时钟中断，也就是IR[0-7]对应的是0x20-0x27
	outb(PIC_M_DATA, 0x04);    // ICW3：IR2接从片
	outb(PIC_M_DATA, 0x01);    // ICW4：8086模式，EOI模式为主动发送

	// 初始化从片
	outb(PIC_S_CTRL, 0x11);    // ICW1：边沿触发，级联8259，需要ICW4
	outb(PIC_S_DATA, 0x28);    // ICW2：起始中断向量号为0x28，也就是IR[8-15]为0x28-0x2F
	outb(PIC_S_DATA, 0x02);    // ICW3：设置从片连接到主片的IR2引脚
	outb(PIC_S_DATA, 0x01);    // ICW4：8086模式，EOI模式为主动发送

	// 打开时钟中断和键盘中断，其他全部关闭
	outb(PIC_M_DATA, 0xfc);
	outb(PIC_S_DATA, 0xff);

	put_str("    pic_init done\n");
}

/* 创建中断门描述符 */
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function) {
	p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
	p_gdesc->selector = SELECTOR_K_CODE;                                        // DPL_CODE = 0
	p_gdesc->dcount = 0;
	p_gdesc->attribute = attr;
	p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/* 初始化中断描述符表 */
static void idt_desc_init(void) {
	int i;
	for (i = 0; i < IDT_DESC_CNT; i++) {
		make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);    // DPL_GATE = 0
	}
	put_str("    idt_desc_init done\n");
}

/* 通用的中断处理函数，一般用在异常出现时的处理 */
static void general_intr_handler(uint8_t vec_nr) {
	if (vec_nr == 0x27 || vec_nr == 0x2f) {
		// IRQ7和IRQ15会产生伪中断（squrious interrupt），无需处理，0x2f中断对应从片8259A上的最后一个IRQ引脚，为保留项
		return;
	}

	// 将光标置为0，从屏幕左上角清出一片打印异常信息的区域，方便阅读
	set_cursor(0);
	int cursor_pos = 0;
	while (cursor_pos < 320) {
		put_char(' ');
		cursor_pos++;
	} 

	set_cursor(0);     // 重置光标为屏幕左上角
	put_str("!!!!!!!    exception message begin    !!!!!!!\n");
	set_cursor(88);    // 从第2行第8个字符开始打印
	put_str(intr_name[vec_nr]);
	
	// 若为缺页异常Pagefault，将缺失的地址打印出来并悬听
	if (vec_nr == 14) {
		int page_fault_vaddr = 0;
		// cr2寄存器会存放造成缺页异常的地址
		asm ("movl %%cr2, %0" : "=r" (page_fault_vaddr));
		put_str("\npage fault addr is ");
		put_int(page_fault_vaddr);
	}

	put_str("\n!!!!!!!    exception message end    !!!!!!!\n");

	// CPU收到中断信号后进入中断处理程序，同时将IF置0关闭中断，防止中断嵌套，
	// 因此不会出现调度其他线程的情况，下面的死循环不会再被中断
	while (1);
}

/* 完成一般中断处理函数注册及异常名称注册 */
static void exception_init(void) {
	int i;
	for (i = 0; i < IDT_DESC_CNT; i++) {
		// idt_table数组中的函数是在进入中断后根据中断向量号调用的，即kernel.S中call [idt_table + %1 * 4]
		// 默认为general_intr_handler，以后会由register_handler来注册具体处理函数
		idt_table[i] = general_intr_handler;
		// 先统一赋值为unknown
		intr_name[i] = "unknown";
	}

	intr_name[0] = "#DE Divide Error";	
	intr_name[1] = "#DB Debug Exception";
	intr_name[2] = "NMI Interrupt";
	intr_name[3] = "#BP Breakpoint Exception";
	intr_name[4] = "#OF Overflow Exception";
	intr_name[5] = "#BR BOUND Range Exceeded Exception";
	intr_name[6] = "#UD Invalid Opcode Exception";
	intr_name[7] = "#NM Device Not Available Exception";
	intr_name[8] = "#DF Double Fault Exception";
	intr_name[9] = "Coprocessor Segment Overrun";
	intr_name[10] = "#TS Invalid TSS Exception";
	intr_name[11] = "#NP Segment Not Present"; 
	intr_name[12] = "#SS Stack Fault Exception"; 
	intr_name[13] = "#GP General Protection Exception";  
	intr_name[14] = "#PF Page-Fault Exception";
	// 第15号是intel保留项，未使用 
	intr_name[16] = "#MF x87 FPU Floating-Point Error";
	intr_name[17] = "#AC Alignment Check Exception";
	intr_name[18] = "#MC Machine-Check Exception";
	intr_name[19] = "#XF SIMD Floating-Point Exception";
}

/* 为vector_no号中断设置中断处理程序function */
void register_handler(uint8_t vector_no, intr_handler function) {
	idt_table[vector_no] = function;
}

/* 完成有关中断的所有初始化工作 */
void idt_init(void) {
	put_str("idt_init start\n");
	idt_desc_init();     // 初始化中断描述符表
	exception_init();    // 异常名初始化并注册通常的中断处理函数
	pic_init();          // 初始化8259A

	// 加载idt（虚拟地址）
	// 这里需要注意，之前多了个括号，丢失了高16位，使用内核页目录表不会出错，但切换到用户进程页目录表会出错，找不到idt入口地址
	// uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)((uint32_t)idt << 16));
	uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
	asm volatile("lidt %0" : : "m" (idt_operand));
	put_str("idt_init done\n");
}

/* 开中断并返回开中断前的状态 */
enum intr_status intr_enable() {
	enum intr_status old_status;
	// 如果中断处于打开状态
	if (INTR_ON == intr_get_status()) {
		old_status = INTR_ON;
		return old_status;
	// 如果中断处于关闭状态
	} else {
		old_status = INTR_OFF;
		asm volatile("sti");                   // 开中断，sti指令将eflags寄存器IF位置1
		return old_status;
	}
}

/* 关中断并返回关中断前的状态 */
enum intr_status intr_disable() {
	enum intr_status old_status;
	if (INTR_ON == intr_get_status()) {
		old_status = INTR_ON;
		asm volatile("cli" : : : "memory");    // 关中断，cli指令将eflags寄存器IF位置0
		return old_status;
	} else {
		old_status = INTR_OFF;
		return old_status;
	}
}

/* 将中断状态设置为status，返回修改前的状态 */
enum intr_status intr_set_status(enum intr_status status) {
	return status & INTR_ON ? intr_enable() : intr_disable();
}

/* 获取当前中断状态 */
enum intr_status intr_get_status() {
	uint32_t eflags = 0;
	GET_EFLAGS(eflags);
	return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}