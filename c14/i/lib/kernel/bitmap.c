#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
//#include "interrupt.h"
#include "debug.h"

/* 将位图btmp初始化 */
void bitmap_init(struct bitmap* btmp) {
	memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

/* 判断bit_idx位是否为1，若为1，则返回true，否则返回false */
bool bitmap_scan_test(struct bitmap* btmp, uint32_t bit_idx) {
	uint32_t byte_idx = bit_idx / 8;    // 索引数组
	uint32_t bit_odd = bit_idx % 8;     // 索引位
	return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

/* 在位图中申请连续cnt个位，成功则返回其起始位下标，失败则返回-1 */
int bitmap_scan(struct bitmap* btmp, uint32_t cnt) {
	uint32_t idx_byte = 0;

	// 找到第一个可以分配的字节
	while ((0xff == btmp->bits[idx_byte]) && (idx_byte < btmp->btmp_bytes_len)) {
		idx_byte++;    // 该字节全为1，已经没有空闲位可以分配
	}

	ASSERT(idx_byte < btmp->btmp_bytes_len);

	if (idx_byte == btmp->btmp_bytes_len) {
		return -1;     // 内存池已经没有可用空间
	}

	// 在该字节中查找第一个空闲位的索引
	int idx_bit = 0;
	while ((uint8_t)(BITMAP_MASK << idx_bit) & btmp->bits[idx_byte]) {
		idx_bit++;
	}

	// 空闲位在位图中的下标
	int bit_idx_start = idx_byte * 8 + idx_bit;

	// 如果只需要一个单位的资源，则已经满足返回条件
	if (1 == cnt) {
		return bit_idx_start;
	}

	// 剩余需要判断的位
	uint32_t bit_left = (btmp->btmp_bytes_len * 8 - bit_idx_start);
	uint32_t next_bit = bit_idx_start + 1;
	uint32_t count = 1;    // 用于记录找到的连续空闲位的数目
	bit_idx_start = -1;    // 返回值初始化为-1，表示没有找到足够的连续空闲位
	while (bit_left-- > 0) {
		// 若下一位为空闲位，count加1
		if (!(bitmap_scan_test(btmp, next_bit))) {
			count++;
		// 否则，count置0，重新开始寻找
		} else {
			count = 0;
		}

		// 如果成功找到连续的cnt个空闲位，则跳出循环
		if (count == cnt) {
			bit_idx_start = next_bit - cnt + 1;
			break;
		}

		// 继续判断下一位
		next_bit++;
	}
	return bit_idx_start;
}

/* 将位图btmp的bit_idx位置为value */
void bitmap_set(struct bitmap* btmp, uint32_t bit_idx, int8_t value) {
	ASSERT((value == 0) || (value == 1));
	uint32_t byte_idx = bit_idx / 8;
	uint32_t bit_odd = bit_idx % 8;

	// 如果value为1
	if (value) {
		btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
	// 如果value为0
	} else {
		btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd);
	}
}
