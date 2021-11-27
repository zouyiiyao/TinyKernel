#ifndef __LIB_IO_H
#define __LIB_IO_H

#include "stdint.h"

/* 向端口port写入一个字节 */
static inline void outb(uint16_t port, uint8_t data) {
	// 对port指定N表示0-255，d表示用dx存储端口号
	// %b0表示对应al，%w1表示对应dx
	asm volatile ("outb %b0, %w1" : : "a" (data), "Nd" (port));
}

/* 将addr处起始的word_cnt个字写入端口port */
static inline void outsw(uint16_t port, const void* addr, uint32_t word_cnt) {
	// +表示此限制既做输入，又做输出
	// +S表示用寄存器约束S将变量addr的值约束到esi中
	// outsw是把ds:esi处的16位的内容写入port端口，注意各个段寄存器的内容已经在loader中初始化过了
	asm volatile ("cld; rep outsw" : "+S" (addr), "+c" (word_cnt) : "d" (port));
}

/* 从端口port读取一个字节并返回 */
static inline uint8_t inb(uint16_t port) {
	uint8_t data;
	asm volatile ("inb %w1, %b0" : "=a" (data) : "Nd" (port));
	return data;
}

/* 将从端口port读入的word_cnt个字写入addr */
static inline void insw(uint16_t port, void* addr, uint32_t word_cnt) {
	// 内联汇编中volatile表示禁止指令重排序，memory提示编译器内存区域已经发生改变，之后不要使用寄存器缓存的数据，而是直接从内存中获取
	// C语言中volatile关键字用于修饰变量，不允许gcc编译时进行优化，并且该变量不允许缓存在寄存器中，注意volatile不能保证内存访问的原子性
	asm volatile ("cld; rep insw" : "+D" (addr), "+c" (word_cnt) : "d" (port) : "memory");
}

#endif
