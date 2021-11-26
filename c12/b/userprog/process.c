#include "process.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "list.h"
#include "tss.h"
#include "interrupt.h"
#include "string.h"
#include "console.h"

extern void intr_exit(void);

/* 构建用户进程初始上下文信息 */
void start_process(void* filename_) {
	void* function = filename_;
	struct task_struct* cur = running_thread();
	cur->self_kstack += sizeof(struct thread_stack) / sizeof(*cur->self_kstack);
	// 中断栈初始化
	struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;
	proc_stack->edi = 0;
	proc_stack->esi = 0;
	proc_stack->ebp = 0;
	proc_stack->esp_dummy = 0;
	proc_stack->ebx = 0;
	proc_stack->edx = 0;
	proc_stack->ecx = 0;
	proc_stack->eax = 0;

	// 用户程序的特权级为3，操作系统有责任把用户进程所有段选择子的RPL都置为3
	proc_stack->gs = 0;            // 用户态用不上，直接初始化为0
	proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
	proc_stack->eip = function;    // 待执行的用户程序地址
	proc_stack->cs = SELECTOR_U_CODE;
	proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
	// 初始化特权级3的栈，地址0xc0000000下的一页
	proc_stack->esp = (void*)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
	proc_stack->ss = SELECTOR_U_DATA;
	asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (proc_stack) : "memory");
}

/* 激活页表 */
void page_dir_activate(struct task_struct* p_thread) {
	// 若为内核线程，需要重新填充页目录表为0x100000
	uint32_t pagedir_phy_addr = 0x100000;
	
	// 用户进程有自己的页目录表
	if (p_thread->pgdir != NULL) {
		pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
	}

	// 更新页目录寄存器cr3，使新页表生效
	asm volatile ("movl %0, %%cr3" : : "r" (pagedir_phy_addr) : "memory");
}

/* 激活内核线程或用户进程的页表，更新tss中的esp0为进程的特权级0的栈 */
void process_activate(struct task_struct* p_thread) {
	ASSERT(p_thread != NULL);

	// 激活该进程或线程的页表
	page_dir_activate(p_thread);

	// 内核线程特权级本身就是0，处理器进入中断时不会从tss中获取0特权级栈地址，所以不需要更新esp0
	if (p_thread->pgdir) {
		update_tss_esp(p_thread);
	}
}

/* 创建用户进程使用的页目录表，将当前页目录表的表示内核空间的pde复制，成功则返回页目录表的虚拟地址，否则返回NULL */
uint32_t* create_page_dir(void) {
	
	// 用户进程的页目录表不能让用户直接访问到，所以在内核空间申请
	uint32_t* page_dir_vaddr = get_kernel_pages(1);

	if (page_dir_vaddr == NULL) {
		console_put_str("create_page_dir: get_kernel_page failed!");
		return NULL;
	}

	// 1. 先复制pde，0xfffff000 + 0x300 * 4是内核页目录表的第768项
	memcpy((uint32_t*)((uint32_t)page_dir_vaddr + 0x300 * 4), (uint32_t*)(0xfffff000 + 0x300 * 4), 1024);

	// 2. 更新页目录地址
	uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
	// 页目录地址是存入页目录表的最后一项，更新页目录地址为新页目录表的物理地址
	page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

	return page_dir_vaddr;
}

/* 创建用户进程虚拟地址位图 */
void create_user_vaddr_bitmap(struct task_struct* user_prog) {
	// 从USER_VADDR_START = 0x8048000开始分配用户虚拟内存，这是Linux用户进程的入口地址
	user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
	uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
	// 用户进程虚拟地址位图在内核空间分配
	user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
	user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
	bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

/* 创建用户进程 */
void process_execute(void* filename, char* name) {
	// 用户进程PCB，由内核来维护，因此在内核空间中申请
	struct task_struct* thread = get_kernel_pages(1);
	// PCB初始化
	init_thread(thread, name, DEFAULT_PRIO);
	// 创建用户进程虚拟地址位图
	create_user_vaddr_bitmap(thread);
	// 初始化线程栈，通过kernel_thread去执行start_process(filename)
	thread_create(thread, start_process, filename);
	// 创建用户进程使用的页目录表
	thread->pgdir = create_page_dir();

	enum intr_status old_status = intr_disable();
	ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
	list_append(&thread_ready_list, &thread->general_tag);

	ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
	list_append(&thread_all_list, &thread->all_list_tag);
	intr_set_status(old_status);
}
