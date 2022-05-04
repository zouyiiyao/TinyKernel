#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H

#include "stdint.h"
#include "list.h"
#include "memory.h"

/* 每个任务最大可打开文件个数 */
#define MAX_FILES_OPEN_PER_PROC 8

#define TASK_NAME_LEN 16

/* 自定义通用函数类型，它将在很多线程函数中作为形参类型 */
typedef void thread_func(void*);

typedef int16_t pid_t;

/* 进程或线程的状态 */
enum task_status {
	TASK_RUNNING,
	TASK_READY,
	TASK_BLOCKED,
	TASK_WAITING,
	TASK_HANGING,
	TASK_DIED
};

/*
 * 中断栈intr_stack
 * 
 * 此结构用于中断发生时保护程序的上下文环境：
 * 进程或线程被外中断或软中断打断时，会按照此结构压入上下文寄存器，intr_exit中的出栈操作是此结构的逆操作，
 * 初始情况下此栈在线程自己的内核栈中位置固定，位与PCB所在页的最顶端
 */
struct intr_stack {
	uint32_t vec_no;       // kernel.S定义宏VECTOR中push %1压入的中断号
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp_dummy;    // 虽然pushad把esp也压入，但esp是不断变化的，所以会被popad忽略
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;

	uint32_t err_code;     // err_code会被压入在eip之后
	void (*eip)(void);
	uint32_t cs;
	uint32_t eflags;
	void* esp;
	uint32_t ss;
};

/*
 * 线程栈thread_stack
 *
 * 线程自己的栈，用于存储线程中待执行的函数，此结构在线程自己的内核栈中位置不固定，
 * 仅用在switch_to时保存线程环境，实际位置取决于实际运行情况
 */
struct thread_stack {
	uint32_t ebp;
	uint32_t ebx;
	uint32_t edi;
	uint32_t esi;

	// 线程第一次执行时，eip指向待调用的函数kernel_thread，其他时候，eip是指向switch_to的返回地址
	void (*eip)(thread_func* func, void* func_arg);

	// 以下仅供第一次被调度上CPU时使用
	void (*unused_retaddr)(void);    // 参数unused_retaddr只为占位，充当返回地址，并没有实际用上
	thread_func* function;           // 由kernel_thread所调用的函数名
	void* func_arg;                  // 由kernel_thread所调用的函数所需的参数
};

/*
 * 进程或线程的PCB，程序控制块
 */
struct task_struct {
	uint32_t* self_kstack;                            // 各内核/用户线程都用自己的内核栈
	pid_t pid;
	enum task_status status;
	char name[TASK_NAME_LEN];
	uint8_t priority;                                 // 线程优先级，每当ticks用完赋值给它的初值
	uint8_t ticks;                                    // 线程每次在处理器上执行的时钟滴答数
	uint32_t elapsed_ticks;                           // 线程在处理器上运行的总时钟滴答数

	int32_t fd_table[MAX_FILES_OPEN_PER_PROC];        // 打开文件数组，下标为文件描述符

	struct list_elem general_tag;                     // general_tag的作用是用于线程在一般的队列中的结点
	struct list_elem all_list_tag;                    // all_list_tag的作用是用于线程队列thread_all_list中的结点
	
	uint32_t* pgdir;                                  // 进程自己页目录表的虚拟地址，如果为线程，则该项属性为NULL
	struct virtual_addr userprog_vaddr;               // 用户进程的虚拟内存池
	struct mem_block_desc u_block_descs[DESC_CNT];    // 用户进程内存块描述结构数组

	uint32_t cwd_inode_nr;                            // 进程所在工作目录的inode编号
	int16_t parent_pid;                               // 父进程pid
	uint32_t stack_magic;                             // 栈的边界标记，用于检测栈的溢出
};

extern struct list thread_ready_list;
extern struct list thread_all_list;

void thread_create(struct task_struct* pthread, thread_func function, void* func_arg);
void init_thread(struct task_struct* pthread, char* name, int prio);
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);
struct task_struct* running_thread(void);
void schedule(void);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct* pthread);
void thread_yield(void);
pid_t fork_pid(void);
void sys_ps(void);

#endif 
