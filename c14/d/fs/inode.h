#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "stdint.h"
#include "list.h"
#include "ide.h"

/* inode结构 */
struct inode {
	/* inode编号，是inode数组中的下标 */
	uint32_t i_no;

	/* 当此inode是文件时，i_size是文件大小，当此inode是目录时，i_size是该目录下所有目录项大小之和 */
	uint32_t i_size;

	/* 记录此文件被打开的次数 */
	uint32_t i_open_cnts;

	/* 写文件不能并行，进程写文件前检查此标识 */
	bool write_deny;

	/* i_sectors[0-11]是直接块，i_sectors[12]用来存储一级间接块指针 */
	uint32_t i_sectors[13];

	/* 用于加入本分区已打开的inode链表的标识 */
	struct list_elem inode_tag;
};

struct inode* inode_open(struct partition* part, uint32_t inode_no);
void inode_sync(struct partition* part, struct inode* inode, void* io_buf);
void inode_init(uint32_t inode_no, struct inode* new_inode);
void inode_close(struct inode* inode);

#endif