#include "sync.h"
#include "list.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"

/*
 * 初始化信号量
 */
void sema_init(struct semaphore* psema, uint8_t value) {
	psema->value = value;               // 为信号量赋初值
	list_init(&psema->waiters);         // 初始化信号量的等待队列
}

/*
 * 初始化锁plock
 */
void lock_init(struct lock* plock) {
	plock->holder = NULL;
	plock->holder_repeat_nr = 0;
	sema_init(&plock->semaphore, 1);    // 信号量初值赋为1 
}

/*
 * 信号量down操作
 */
void sema_down(struct semaphore* psema) {
	// 关中断来保证原子操作
	enum intr_status old_status = intr_disable();
	// 若value为0，表示已经被别人持有，这里使用while而不是if，因为被唤醒后到被调度还需要一段时间，
	// 真正被调度时需要保证资源未被其他同样申请该信号量线程抢先（即比该线程更早得到调度）
	while (psema->value == 0) {
		ASSERT(!elem_find(&psema->waiters, &running_thread()->general_tag));
		// 以本书的thread_unblock实现方式，前面的while可以用if代替，执行到这里时当前线程不应该已在信号量的waiters队列中
		if (elem_find(&psema->waiters, &running_thread()->general_tag)) {
			PANIC("sema_down: thread blocked has been in waiters_list\n");
		}
		// 若信号量的值等于0，则当前线程（TASK_RUNNING状态，不在就绪队列中）把自己加入该锁的等待队列，同样通过general_tag链接，然后阻塞自己
		list_append(&psema->waiters, &running_thread()->general_tag);
		// 阻塞自己，直到被唤醒
		thread_block(TASK_BLOCKED);
	}

	// 若value为1或被唤醒后，会执行下面的代码，也就是获得了锁
	psema->value--;
	ASSERT(psema->value == 0);
	intr_set_status(old_status);
}

/*
 * 信号量的up操作
 */
void sema_up(struct semaphore* psema) {
	// 关中断来保证原子操作
	enum intr_status old_status = intr_disable();
	ASSERT(psema->value == 0);
	// 如果该锁的等待队列不为空，则从等待队列中取队首线程，将其唤醒，等待当前线程时间片用完后调度执行
	if (!list_empty(&psema->waiters)) {
		struct task_struct* thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters));
		thread_unblock(thread_blocked);
	}
	psema->value++;
	ASSERT(psema->value == 1);
	intr_set_status(old_status);
}

/*
 *  获取锁plock
 */
void lock_acquire(struct lock* plock) {
	// 判断自己已经持有锁未释放又再次申请的情况
	if (plock->holder != running_thread()) {
		sema_down(&plock->semaphore);    // 信号量P操作，原子操作
		plock->holder = running_thread();
		ASSERT(plock->holder_repeat_nr == 0);
		plock->holder_repeat_nr = 1;
	} else {
		plock->holder_repeat_nr++;
	}
}

/*
 * 释放锁plock
 */
void lock_release(struct lock* plock) {
	ASSERT(plock->holder == running_thread());
	if (plock->holder_repeat_nr > 1) {
		plock->holder_repeat_nr--;
		return;
	}
	ASSERT(plock->holder_repeat_nr == 1);

	plock->holder = NULL;          // 把锁的持有者置空，放在V操作之前
	plock->holder_repeat_nr = 0;
	sema_up(&plock->semaphore);    // 信号量V操作，原子操作
}
