#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H

#include "list.h"
#include "stdint.h"
#include "thread.h"

/*
 * 信号量结构
 */
struct semaphore {
	uint8_t value;                 // 信号量的初值，代表资源数量，用于实现锁则为二元信号量
	struct list waiters;           // 用信号量实现锁，一个锁对应一个等待该锁的等待队列
};

/*
 * 锁结构
 */
struct lock {
	struct task_struct* holder;    // 当前锁的持有者
	struct semaphore semaphore;    // 用二元信号量实现锁
	uint32_t holder_repeat_nr;     // 锁的持有者重复申请锁的次数
};

void sema_init(struct semaphore* psema, uint8_t value);
void sema_down(struct semaphore* psema);
void sema_up(struct semaphore* psema);
void lock_init(struct lock* plock);
void lock_acquire(struct lock* plock);
void lock_release(struct lock* plock);

#endif
