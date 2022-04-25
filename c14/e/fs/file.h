#ifndef __FS_FILE_H
#define __FS_FILE_H

#include "stdint.h"
#include "ide.h"
#include "dir.h"
#include "global.h"

/* 文件结构，仅存在于内存中 */
struct file {
	uint32_t fd_pos;           // 记录当前文件操作的偏移量，0为文件起始，-1为文件末尾
	uint32_t fd_flag;          // 文件操作标识，如O_RDONLY
	struct inode* fd_inode;    // inode指针，指向inode队列part->open_inodes中的inode
};

/* 标准输入输出描述符 */
enum std_fd {
	stdin_no,     // 0，标准输入
	stdout_no,    // 1，标准输出
	stderr_no,    // 2，标准错误
};

/* 位图类型 */
enum bitmap_type {
	INODE_BITMAP,    // inode位图
	BLOCK_BITMAP,    // 块位图
};

/* 操作系统可打开最大文件数，打开文件表大小 */
#define MAX_FILE_OPEN 32

extern struct file file_table[MAX_FILE_OPEN];

int32_t inode_bitmap_alloc(struct partition* part);
int32_t block_bitmap_alloc(struct partition* part);
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag);
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp);
int32_t get_free_slot_in_global(void);
int32_t pcb_fd_install(int32_t global_fd_idx);
int32_t file_open(uint32_t inode_no, uint8_t flag);
int32_t file_close(struct file* file);
int32_t file_write(struct file* file, const void* buf, uint32_t count);

#endif
