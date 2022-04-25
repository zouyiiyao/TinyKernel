#include "inode.h"
#include "fs.h"
#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio_kernel.h"
#include "string.h"
#include "super_block.h"

/* 用来存储inode位置 */
struct inode_position {
	bool two_sec;         // inode是否跨扇区
	uint32_t sec_lba;     // inode所在扇区号
	uint32_t off_size;    // inode所在扇区内的字节偏移量
};

/* 获取inode所在的扇区和扇区内的偏移量 */
static void inode_locate(struct partition* part, uint32_t inode_no, struct inode_position* inode_pos) {
	/* inode_table在硬盘上连续存储 */
	ASSERT(inode_no < 4096);

	uint32_t inode_table_lba = part->sb->inode_table_lba;
	uint32_t inode_size = sizeof(struct inode);
	uint32_t off_size = inode_no * inode_size;    // 第inode_no号inode结点相对于inode_table_lba的字节偏移量
	uint32_t off_sec = off_size / 512;            // 第inode_no号inode结点相对于inode_table_lba的扇区偏移量
	uint32_t off_size_in_sec = off_size % 512;    // 第inode_no号inode结点在其所在扇区中的起始地址

	/* 判断此inode结点是否跨越两个扇区 */
	uint32_t left_in_sec = 512 - off_size_in_sec;
	if (left_in_sec < inode_size) {    // 若扇区内剩下的空间不足以容纳一个inode，则跨越两个扇区
		inode_pos->two_sec = true;
	} else {
		inode_pos->two_sec = false;
	}
	inode_pos->sec_lba = inode_table_lba + off_sec;
	inode_pos->off_size = off_size_in_sec;
}

/* 将inode写入到分区part */
void inode_sync(struct partition* part, struct inode* inode, void* io_buf) {
	/* io_buf是用于硬盘io的内存缓冲区 */
	uint8_t inode_no = inode->i_no;
	struct inode_position inode_pos;
	/* 将inode位置信息存入inode_pos */
	inode_locate(part, inode_no, &inode_pos);
	ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

	/* inode中的成员inode_tag和i_open_cnts在硬盘中是不需要的，它们只在内存中记录链表位置和被多少进程共享 */
	struct inode pure_inode;
	memcpy(&pure_inode, inode, sizeof(struct inode));
	/* 以下inode的三个成员只存在于内存中，现在将inode同步到硬盘，清除这三项即可 */
	pure_inode.i_open_cnts = 0;
	pure_inode.write_deny = false;
	pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

	char* inode_buf = (char*)io_buf;
	/* 若inode跨两个扇区，就要读出两个扇区再写入两个扇区 */
	if (inode_pos.two_sec) {
		/* 读写硬盘是以扇区为单位，若写入的数据小于一扇区，要将原硬盘上的内容先读出来再和新数据拼成一扇区后写入 */

		/* inode在partition_format中写入硬盘时是连续写入的，所以读入两块扇区 */
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);

		/* 开始将待写入的inode拼入这两个扇区中的相应位置 */
		memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));

		/* 将拼接好的两个扇区数据写入硬盘 */
		ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
	} else {
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
		memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
		ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
	}
}

/* 根据inode号返回相应的inode结点 */
struct inode* inode_open(struct partition* part, uint32_t inode_no) {
	/* 先在已打开inode链表中找，此链表是为提速创建的缓存 */
	struct list_elem* elem = part->open_inodes.head.next;
	struct inode* inode_found;
	while (elem != &part->open_inodes.tail) {
		inode_found = elem2entry(struct inode, inode_tag, elem);
		if (inode_found->i_no == inode_no) {
			inode_found->i_open_cnts++;
			return inode_found;
		}
		elem = elem->next;
	}

	/* open_inodes链表中没找到，下面从硬盘上读取此inode并加入到open_inodes链表 */
	struct inode_position inode_pos;

	/* inode位置信息会存入inode_pos，包括inode所在扇区地址和扇区内的字节偏移量 */
	inode_locate(part, inode_no, &inode_pos);

	/* 为使通过sys_malloc创建的新inode被所有任务共享，需要将inode置于内核空间，故需要临时将cur_pcb->pgdir置为NULL */
	struct task_struct* cur = running_thread();
	uint32_t* cur_pagedir_bak = cur->pgdir;
	cur->pgdir = NULL;
	/* 以上三行代码使下面分配的内存位于内核区 */
	inode_found = (struct inode*)sys_malloc(sizeof(struct inode));
	/* 恢复pgdir */
	cur->pgdir = cur_pagedir_bak;

	char* inode_buf;
	if (inode_pos.two_sec) {    // inode跨扇区
		inode_buf = (char*)sys_malloc(1024);
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
	} else {                    // inode未跨扇区
		inode_buf = (char*)sys_malloc(512);
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
	}
	memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));

	/* 由于该inode马上可能要被用到，故将其插入到队首加速检索 */
	list_push(&part->open_inodes, &inode_found->inode_tag);
	inode_found->i_open_cnts = 1;

	sys_free(inode_buf);
	return inode_found;
}

/* 关闭inode */
void inode_close(struct inode* inode) {
	/* 若没有进程再打开此文件，将此inode去掉并释放空间 */
	enum intr_status old_status = intr_disable();
	if (--inode->i_open_cnts == 0) {
		/* 将inode从打开inode链表中移除 */
		list_remove(&inode->inode_tag);
		/* inode_open中，使用内核空间实现inode被所有进程共享，释放inode时也要确保是内核内存池 */
		struct task_struct* cur = running_thread();
		uint32_t* cur_pagedir_bak = cur->pgdir;
		cur->pgdir = NULL;
		sys_free(inode);
		cur->pgdir = cur_pagedir_bak;
	}
	intr_set_status(old_status);
}

/* 初始化new_inode */
void inode_init(uint32_t inode_no, struct inode* new_inode) {
	new_inode->i_no = inode_no;
	new_inode->i_size = 0;
	new_inode->i_open_cnts = 0;
	new_inode->write_deny = false;

	/* 初始化块索引数组i_sector */
	uint8_t sec_idx = 0;
	while (sec_idx < 13) {
		new_inode->i_sectors[sec_idx] = 0;    // 块地址为0表示未分配
		sec_idx++;
	}
}

