#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "process.h"
#include "sync.h"
#include "stdio.h"
#include "file.h"
#include "fs.h"

#define PG_SIZE 4096

struct task_struct* main_thread;        // 主线程PCB
struct task_struct* idle_thread;        // idle线程PCB
struct list thread_ready_list;          // 就绪队列
struct list thread_all_list;            // 所有任务队列
static struct list_elem* thread_tag;    // 用于保存队列中的线程结点

struct lock pid_lock;                   // 分配pid锁

extern void switch_to(struct task_struct* cur, struct task_struct* next);
extern void init(void);

/* 系统空闲时运行的线程 */
static void idle(void* arg UNUSED) {
	while (1) {
		thread_block(TASK_BLOCKED);
		// 执行hlt时必须要保证目前处在开中断的情况下
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* 获取当前线程pcb指针 */
struct task_struct* running_thread() {
	uint32_t esp;
	asm ("mov %%esp, %0" : "=g" (esp));
	// 取esp整数部分，即pcb起始地址
	return (struct task_struct*)(esp & 0xfffff000);
}

/* 由kernel_thread去执行function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
	// 执行function前要开中断，避免后面的时钟中断被屏蔽，而无法调度其他线程
	intr_enable();
	function(func_arg);
}

/* 给任务分配pid */
static pid_t allocate_pid(void) {
	static pid_t next_pid = 0;
	lock_acquire(&pid_lock);
	next_pid++;
	lock_release(&pid_lock);
	return next_pid;
}

/* fork进程时为子进程分配pid */
pid_t fork_pid(void) {
	// 调用静态函数allocate_pid
	return allocate_pid();
}

/* 初始化线程栈thread_stack，将待执行的函数和参数放到thread_stack中相应的位置 */
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
	// 先预留中断使用栈的空间，可见thread.h中定义的结构
	pthread->self_kstack -= sizeof(struct intr_stack) / sizeof(*pthread->self_kstack);

	// 再留出线程栈空间，可见thread.h中定义
	pthread->self_kstack -= sizeof(struct thread_stack) / sizeof(*pthread->self_kstack);
	struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
	// 构造kernel_thread调用环境，注意返回地址只是占位，从kernel_thread内ret是无效的，只是使得进入kernel_thread执行时栈的情况和call指令调用一样
	kthread_stack->eip = kernel_thread;
	kthread_stack->function = function;
	kthread_stack->func_arg = func_arg;
	kthread_stack->ebp = 0; 
	kthread_stack->ebx = 0; 
	kthread_stack->esi = 0; 
	kthread_stack->edi = 0; 
}

/* 初始化线程基本信息 */
void init_thread(struct task_struct* pthread, char* name, int prio) {
	memset(pthread, 0, sizeof(*pthread));
	strcpy(pthread->name, name);
	pthread->pid = allocate_pid();

	// 由于把main函数也封装成一个线程，并且它一直是运行的，故将其直接设为TASK_RUNNING	
	if (pthread == main_thread) {
		pthread->status = TASK_RUNNING;
	} else {
		pthread->status = TASK_READY;
	}

	// self_kstack是线程自己在内核态下使用的栈地址，在PCB使用的物理页最高地址处，往下增长
	pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
	pthread->priority = prio;
	pthread->ticks = prio;
	pthread->elapsed_ticks = 0;
	pthread->pgdir = NULL;

	// 预留标准输入输出
	pthread->fd_table[0] = 0;
	pthread->fd_table[1] = 1;
	pthread->fd_table[2] = 2;
	// 其余全部置-1
	uint8_t fd_idx = 3;
	while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
		pthread->fd_table[fd_idx] = -1;
		fd_idx++;
	}

	// 以根目录为默认工作目录
	pthread->cwd_inode_nr = 0;

	// -1表示没有父进程
	pthread->parent_pid = -1;

	// 自定义的魔数
	pthread->stack_magic = 0x19870916;
}

/* 创建一个优先级为prio的线程，线程名为name，线程所执行的函数是function(func_arg) */
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
	// PCB都位于内核空间，包括用户进程的PCB也是在内核空间
	struct task_struct* thread = get_kernel_pages(1);

	init_thread(thread, name, prio);
	thread_create(thread, function, func_arg);

	// 确保之前不在就绪队列中
	ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
	// 加入就绪线程队列
	list_append(&thread_ready_list, &thread->general_tag);

	// 确保之前不在全部线程队列中
	ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
	// 加入全部线程队列
	list_append(&thread_all_list, &thread->all_list_tag);
	
	return thread;
}

/*
 * 将kernel中的main函数完善为主线程
 */
static void make_main_thread(void) {
	// 因为main线程早已运行，咱们在loader.S中进入内核时的mov esp, 0xc009f000就是为其预留pcb的，
	// 因此pcb地址为0xc009e000，不需要通过get_kernel_pages另分配一页
	main_thread = running_thread();
	init_thread(main_thread, "main", 31);

	// main函数是当前线程，当前线程不在thread_ready_list中，所以只将其加在thread_all_list中
	ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
	list_append(&thread_all_list, &main_thread->all_list_tag);
}

/*
 * 实现任务调度
 */
void schedule() {
	
	// schedule一定是在关中断情况下被调用
	ASSERT(intr_get_status() == INTR_OFF);

	struct task_struct* cur = running_thread();
	// 若此线程只是CPU时间片到了，将其加入到就绪队列尾
	if (cur->status == TASK_RUNNING) {
		ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
		list_append(&thread_ready_list, &cur->general_tag);
		// 重新将该线程的ticks置为其priority
		cur->ticks = cur->priority;
		cur->status = TASK_READY;
	// 若此线程需要某事件发生后才能继续上CPU执行，不将其加入就绪队列
	} else {
	}

	/* 如果就绪队列中没有可运行的任务，就唤醒idle */
	if (list_empty(&thread_ready_list)) {
		thread_unblock(idle_thread);
	}

	ASSERT(!list_empty(&thread_ready_list));
	
	thread_tag = NULL;
	// 将thread_ready_list队列中的第一个就绪线程弹出，准备将其调度上CPU
	thread_tag = list_pop(&thread_ready_list);
	struct task_struct* next = elem2entry(struct task_struct, general_tag, thread_tag);
	next->status = TASK_RUNNING;

	// 激活任务页表，更新tss中esp0为进程的特权级0的栈
	process_activate(next);	

	// 调用switch_to函数，切换当前线程pcb和下一个被调度线程pcb
	switch_to(cur, next);
}

/*
 * 当前线程将自己阻塞，标志其状态为stat
 */
void thread_block(enum task_status stat) {
	// stat取值为TASK_BLOCKED | TASK_WAITING | TASK_HANGING，这三种状态不会被调度
	ASSERT((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING));

	// 函数内部保证原子性
	enum intr_status old_status = intr_disable();
	struct task_struct* cur_thread = running_thread();
	cur_thread->status = stat;    // 置其状态为stat
	schedule();                   // 将当前线程换下处理器，并非通过中断调用，但下一个被调度到的线程一定返回到中断或返回到这里执行（会开中断）

	// 待当前线程被解除阻塞后才继续运行下面的intr_set_status
	intr_set_status(old_status);
}

/*
 * 将线程pthread解除阻塞
 */
void thread_unblock(struct task_struct* pthread) {
	// 函数内部保证原子性
	enum intr_status old_status = intr_disable();
	ASSERT((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING));

	if (pthread->status != TASK_READY) {
		ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
		if (elem_find(&thread_ready_list, &pthread->general_tag)) {
			PANIC("thread_unblock: blocked thread in ready_list\n");
		}
		// 放到队列最前面，等当前线程时间片执行完立即调度
		list_push(&thread_ready_list, &pthread->general_tag);
		pthread->status = TASK_READY;
	}
	intr_set_status(old_status);
}

/* 主动让出CPU，换其他线程运行 */
void thread_yield(void) {
	struct task_struct* cur = running_thread();
	enum intr_status old_status = intr_disable();
	ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
	list_append(&thread_ready_list, &cur->general_tag);
	cur->status = TASK_READY;
	schedule();
	intr_set_status(old_status);
}

/* 以填充空格的方式输出buf */
static void pad_print(char* buf, int32_t buf_len, void* ptr, char format) {
	memset(buf, 0, buf_len);
	uint8_t out_pad_0idx = 0;
	switch (format) {
		case 's':
			out_pad_0idx = sprintf(buf, "%s", ptr);
			break;
		case 'd':
			out_pad_0idx = sprintf(buf, "%d", *((int16_t*)ptr));
			break;
		case 'x':
			out_pad_0idx = sprintf(buf, "%x", *((uint32_t*)ptr));
			break;
	}

	/* 用空格补齐 */
	while (out_pad_0idx < buf_len) {
		buf[out_pad_0idx] = ' ';
		out_pad_0idx++;
	}

	sys_write(stdout_no, buf, buf_len - 1);
}

/* 用于list_traversal全部线程队列时的回调函数 */
static bool elem2thread_info(struct list_elem* pelem, int arg UNUSED) {
	struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);

	char out_pad[16] = {0};

	/* 打印pid */
	pad_print(out_pad, 16, &pthread->pid, 'd');

	/* 打印父进程pid */
	if (pthread->parent_pid == -1) {
		pad_print(out_pad, 16, "NULL", 's');
	} else {
		pad_print(out_pad, 16, &pthread->parent_pid, 'd');
	}

	/* 打印进程状态 */
	switch (pthread->status) {
		case 0:
			pad_print(out_pad, 16, "RUNNING", 's');
			break;
		case 1:
			pad_print(out_pad, 16, "READY", 's');
			break;
		case 2:
			pad_print(out_pad, 16, "BLOCKED", 's');
			break;
		case 3:
			pad_print(out_pad, 16, "WAITING", 's');
			break;
		case 4:
			pad_print(out_pad, 16, "HANGING", 's');
			break;
		case 5:
			pad_print(out_pad, 16, "DIED", 's');
			break;
	}

	/* 打印运行总时间 */
	pad_print(out_pad, 16, &pthread->elapsed_ticks, 'x');

	/* 打印任务名称 */
	memset(out_pad, 0, 16);
	ASSERT(strlen(pthread->name) < 17);
	memcpy(out_pad, pthread->name, strlen(pthread->name));
	strcat(out_pad, "\n");
	sys_write(stdout_no, out_pad, strlen(out_pad));

	return false;
}

/* 打印任务列表信息 */
void sys_ps(void) {
	char* ps_title = "PID            PPID           STAT           TICKS          COMMAND\n";
	sys_write(stdout_no, ps_title, strlen(ps_title));

	list_traversal(&thread_all_list, elem2thread_info, 0);
}

/*
 * 初始化线程环境
 */
void thread_init(void) {
	put_str("thread_init start\n");
	list_init(&thread_ready_list);
	list_init(&thread_all_list);
	lock_init(&pid_lock);

	// 先创建第一个用户进程init，这是第一个进程，init进程的pid为1
	process_execute(init, "init");

	// 将当前main函数创建为线程
	make_main_thread();

	// 创建idle线程
	idle_thread = thread_start("idle", 10, idle, NULL);

	put_str("thread_init done\n");
}
