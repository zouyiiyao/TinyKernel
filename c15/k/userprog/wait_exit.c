#include "wait_exit.h"
#include "global.h"
#include "debug.h"
#include "thread.h"
#include "list.h"
#include "stdio_kernel.h"
#include "memory.h"
#include "bitmap.h"
#include "fs.h"

/* 释放用户进程占用资源，除pcb和页目录表外 */
static void release_prog_resource(struct task_struct* release_thread) {
	uint32_t* pgdir_vaddr = release_thread->pgdir;

	uint16_t user_pde_nr = 768, pde_idx = 0;
	uint32_t pde = 0;
	uint32_t* v_pde_ptr = NULL;

	uint16_t user_pte_nr = 1024, pte_idx = 0;
	uint32_t pte = 0;
	uint32_t* v_pte_ptr = NULL;

	uint32_t* first_pte_vaddr_in_pde = NULL;
	uint32_t pg_phy_addr = 0;

	/* 1. 回收用户空间物理页 */
	while (pde_idx < user_pde_nr) {
		v_pde_ptr = pgdir_vaddr + pde_idx;
		pde = *v_pde_ptr;
		/* 页目录项p为1，说明可能有页表 */
		if (pde & 0x00000001) {
			/* 一个页表代表4MB内存，即0x400000 */
			first_pte_vaddr_in_pde = pte_ptr(pde_idx * 0x400000);
			pte_idx = 0;
			while (pte_idx < user_pte_nr) {
				v_pte_ptr = first_pte_vaddr_in_pde + pte_idx;
				pte = *v_pte_ptr;
				/* 物理页已分配 */
				if (pte & 0x00000001) {
					/* 将pte中记录的物理页在内存池位图中清0 */
					pg_phy_addr = pte & 0xfffff000;
					pfree(pg_phy_addr);
				}
				pte_idx++;
			}
			/* 将pde中记录的物理页在内存池位图中清0 */
			pg_phy_addr = pde & 0xfffff000;
			pfree(pg_phy_addr);
		}
		pde_idx++;
	}

	/* 2. 回收用户虚拟地址池所占物理内存 */
	uint32_t bitmap_pg_cnt = release_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len / PG_SIZE;
	uint8_t* user_vaddr_pool_bitmap = release_thread->userprog_vaddr.vaddr_bitmap.bits;
	mfree_page(PF_KERNEL, user_vaddr_pool_bitmap, bitmap_pg_cnt);

	/* 3. 关闭进程打开文件 */
	uint8_t fd_idx = 3;
	while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
		if (release_thread->fd_table[fd_idx] != -1) {
			sys_close(fd_idx);
		}
		fd_idx++;
	}
}

/* list_traversal的回调函数，判断pelem的parent_pid是否是ppid */
static bool find_child(struct list_elem* pelem, int32_t ppid) {
	struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
	if (pthread->parent_pid == ppid) {
		return true;
	}
	return false;
}

/* list_traversal的回调函数，查找状态为TASK_HANGING的任务，即子进程执行exit后的状态 */
static bool find_hanging_child(struct list_elem* pelem, int32_t ppid) {
	struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
	if (pthread->parent_pid == ppid && pthread->status == TASK_HANGING) {
		return true;
	}
	return false;
}

/* list_traversal的回调函数，将进程ppid的一个子进程过继给init */
static bool init_adopt_a_child(struct list_elem* pelem, int32_t ppid) {
	struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
	if (pthread->parent_pid == ppid) {
		pthread->parent_pid = 1;
	}
	return false;    // 一直往下查找
}

/* 等待子进程调用exit，将子进程的退出状态保存到status，成功返回子进程pid，失败返回-1 */
pid_t sys_wait(int32_t* status) {
	struct task_struct* parent_thread = running_thread();

	while (1) {
		/* 判断是否有已经调用exit的子进程，即进入了TASK_HANGING状态 */
		struct list_elem* child_elem = list_traversal(&thread_all_list, find_hanging_child, parent_thread->pid);
		if (child_elem != NULL) {    // 子进程只等待父进程回收，就可以结束使命
			struct task_struct* child_thread = elem2entry(struct task_struct, all_list_tag, child_elem);
			*status = child_thread->exit_status;

			/* 先获取pid，再执行thread_exit，因为会回收子进程pcb */
			uint16_t child_pid = child_thread->pid;

			/* 回收子进程的pcb和页目录表，从任务队列中删除该任务 */
			thread_exit(child_thread, false);
			/* 传入false，使thread_exit调用后回到父进程此处，至此子进程彻底消失了 */

			return child_pid;
		}

		/* 判断是否有子进程 */
		child_elem = list_traversal(&thread_all_list, find_child, parent_thread->pid);
		if (child_elem == NULL) {    // 没有子进程，出错返回
			return -1;
		/* 所有子进程都未运行完成，即还没调用exit，则进入TASK_WAITING状态，*/
		/* 操作系统不再调度父进程运行，直到子进程执行exit时将父进程唤醒 */
		} else {
			thread_block(TASK_WAITING);
		}
	}
}

/* 子进程结束自己时调用 */
void sys_exit(int32_t status) {
	struct task_struct* child_thread = running_thread();
	child_thread->exit_status = status;
	if (child_thread->parent_pid == -1) {
		PANIC("sys_exit: child_thread->parent_pid is -1\n");
	}

	/* 进程结束时，将其子进程全部过继给init */
	list_traversal(&thread_all_list, init_adopt_a_child, child_thread->pid);

	/* 回收进程资源 */
	release_prog_resource(child_thread);

	/* 如果父进程在等待子进程退出，则将其唤醒，回收最后的资源 */
	struct task_struct* parent_thread = pid2thread(child_thread->parent_pid);
	if (parent_thread->status == TASK_WAITING) {
		thread_unblock(parent_thread);
	}

	/* 将自己进入TASK_HANGING状态，操作系统不再调度，等待父进程回收并获取其退出状态 */
	thread_block(TASK_HANGING);
}
