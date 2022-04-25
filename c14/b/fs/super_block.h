#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_H

#include "stdint.h"

/* 超级块 */
struct super_block {
	/* 支持多文件系统的操作系统通过此标识来识别文件系统类型 */
	uint32_t magic;                 // 用来标识文件系统类型
	uint32_t sec_cnt;               // 本分区总共扇区数
	uint32_t inode_cnt;             // 本分区总共inode数
	uint32_t part_lba_base;         // 本分区的起始扇区lba地址

	uint32_t block_bitmap_lba;      // 块位图本身起始扇区lba地址
	uint32_t block_bitmap_sects;    // 块位图本身占用的扇区数量

	uint32_t inode_bitmap_lba;      // inode位图本身起始扇区lba地址
	uint32_t inode_bitmap_sects;    // inode位图本身占用的扇区数量

	uint32_t inode_table_lba;       // inode表起始扇区lba地址
	uint32_t inode_table_sects;     // inode表占用的扇区数量

	uint32_t data_start_lba;        // 数据区开始的第一个扇区号
	uint32_t root_inode_no;         // 根目录所在的inode号
	uint32_t dir_entry_size;        // 目录项大小

	uint8_t pad[460];               // 加上460字节，凑够512字节1扇区大小
} __attribute__ ((packed));

#endif
