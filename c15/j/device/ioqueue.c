#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

/* 初始化io队列ioq */
void ioqueue_init(struct ioqueue* ioq) {
	lock_init(&ioq->lock);    // 初始化io队列的锁
	ioq->producer = NULL;     // 生产者置空
	ioq->consumer = NULL;     // 消费者置空
	ioq->head = 0;            // 队列的首指针指向缓冲区数组第0个位置
	ioq->tail = 0;            // 队列的尾指针指向缓冲区数组第0个位置
}

/* 返回pos在缓冲区中的下一个位置值 */
static int32_t next_pos(int32_t pos) {
	return (pos + 1) % bufsize;
}

/* 判断队列是否已满 */
bool ioq_full(struct ioqueue* ioq) {
	ASSERT(intr_get_status() == INTR_OFF);
	return next_pos(ioq->head) == ioq->tail;
}

/* 判断队列是否已空 */
static bool ioq_empty(struct ioqueue* ioq) {
	ASSERT(intr_get_status() == INTR_OFF);
	return ioq->head == ioq->tail;
}

/* 使当前生产者或消费者在此缓冲区上等待 */
static void ioq_wait(struct task_struct** waiter) {
	ASSERT(*waiter == NULL && waiter != NULL);
	*waiter = running_thread();    // 调用时生产者或消费者是当前线程
	thread_block(TASK_BLOCKED);
}

/* 将正在等待的生产者或消费者唤醒 */
static void wakeup(struct task_struct** waiter) {
	ASSERT(*waiter != NULL);
	thread_unblock(*waiter);
	*waiter = NULL;
}

/* 消费者从ioq队列中获取一个字符 */
char ioq_getchar(struct ioqueue* ioq) {
	ASSERT(intr_get_status() == INTR_OFF);

	// 若缓冲区为空，把消费者ioq->consumer更新为当前线程，目的是将来生产者往缓冲区添加数据后，生产者知道应该唤醒哪个消费者
	while (ioq_empty(ioq)) {
		lock_acquire(&ioq->lock);
		// 此处获得锁之后，应该判断当前缓冲区是否为空，如果缓冲区不为空，不需要等待生产者将其唤醒
		if (ioq_empty(ioq)) {
			ioq_wait(&ioq->consumer);
		}
		lock_release(&ioq->lock);
	}

	char byte = ioq->buf[ioq->tail];    // 从缓冲区中取出数据
	ioq->tail = next_pos(ioq->tail);    // 把读游标移到下一位置

	if (ioq->producer != NULL) {
		wakeup(&ioq->producer);     // 唤醒生产者
	}

	return byte;
}

/* 生产者往ioq队列中写入一个字符byte */
void ioq_putchar(struct ioqueue* ioq, char byte) {
	ASSERT(intr_get_status() == INTR_OFF);

	// 若缓冲区已满，把生产者ioq->producer更新为当前线程，目的是将来缓冲区的数据被消费者取出后，消费者知道唤醒哪个生产者
	while (ioq_full(ioq)) {
		lock_acquire(&ioq->lock);
		if (ioq_full(ioq)) {
			ioq_wait(&ioq->producer);
		}
		lock_release(&ioq->lock);
	}

	ioq->buf[ioq->head] = byte;         // 把字节放入缓冲区中
	ioq->head = next_pos(ioq->head);    // 把写游标移到下一位置

	if (ioq->consumer != NULL) {
		wakeup(&ioq->consumer);     // 唤醒消费者
	}
}

/* 返回环形缓冲区中的数据字节数 */
uint32_t ioq_length(struct ioqueue* ioq) {
	uint32_t len = 0;
	if (ioq->head >= ioq->tail) {
		len = ioq->head - ioq->tail;
	} else {
		len = bufsize - (ioq->tail - ioq->head);
	}
	return len;
}
