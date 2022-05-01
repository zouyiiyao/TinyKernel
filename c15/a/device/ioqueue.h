#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H

#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define bufsize 64

/* 环形队列，解决生产者消费者问题 */
struct ioqueue {
	struct lock lock;
	struct task_struct* producer;    // 生产者，缓冲区不满时就继续往里面放数据，否则就睡眠，此项记录哪个生产者在此缓冲区上睡眠
	struct task_struct* consumer;    // 消费者，缓冲区不空时就继续从里面拿数据，否则就睡眠，此项记录哪个消费者在此缓冲区上睡眠
	char buf[bufsize];               // 缓冲区大小
	int32_t head;                    // 队首，数据往队首处写入
	int32_t tail;                    // 队尾，数据往队尾处读出
};

void ioqueue_init(struct ioqueue* ioq);
bool ioq_full(struct ioqueue* ioq);
char ioq_getchar(struct ioqueue* ioq);
void ioq_putchar(struct ioqueue* ioq, char byte);

#endif
