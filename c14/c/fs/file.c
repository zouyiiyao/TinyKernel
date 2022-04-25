#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio_kernel.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"
#include "global.h"

/* 打开文件表，位于内存中，全局的，所有任务共享 */
struct file file_table[MAX_FILE_OPEN];

/* 从打开文件表file_table中获取一个空闲位，成功返回下标，失败返回-1 */
int32_t get_free_slot_in_global(void) {
	uint32_t fd_idx = 3;
	while (fd_idx < MAX_FILE_OPEN) {
		if (file_table[fd_idx].fd_inode == NULL) {
			break;
		}
		fd_idx++;
	}
	if (fd_idx == MAX_FILE_OPEN) {
		printk("exceed max open files\n");
		return -1;
	}
	return fd_idx;
}

/* 将打开文件表下标安装到进程或线程自己的打开文件数组fd_table中，成功返回文件描述符，即打开文件数组下标，失败返回-1 */
int32_t pcb_fd_install(int32_t global_fd_idx) {
	struct task_struct* cur = running_thread();
	uint8_t local_fd_idx = 3;    // 跨过stdin, stdout, stderr
	while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
		if (cur->fd_table[local_fd_idx] == -1) {    // 文件描述符可用
			cur->fd_table[local_fd_idx] = global_fd_idx;
			break;
		}
		local_fd_idx++;
	}
	if (local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
		printk("exceed max open files_per_proc\n");
		return -1;
	}
	return local_fd_idx;
}

/* 分配一个inode，返回inode号 */
int32_t inode_bitmap_alloc(struct partition* part) {
	int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
	if (bit_idx == -1) {
		return -1;
	}
	bitmap_set(&part->inode_bitmap, bit_idx, 1);
	return bit_idx;
}

/* 分配一个扇区，返回扇区号 */
int32_t block_bitmap_alloc(struct partition* part) {
	int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
	if (bit_idx == -1) {
		return -1;
	}
	bitmap_set(&part->block_bitmap, bit_idx, 1);
	return (part->sb->data_start_lba + bit_idx);
}

/* 将内存中bitmap第bit_idx位所在的512字节同步到硬盘 */
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp) {
	uint32_t off_sec = bit_idx / 4096;           // 扇区偏移量
	uint32_t off_size = off_sec * BLOCK_SIZE;    // 字节偏移量

	uint32_t sec_lba;
	uint8_t* bitmap_off;

	/* 需要被同步到硬盘的位图只有inode_bitmap和block_bitmap */
	switch (btmp) {
		case INODE_BITMAP:
			sec_lba = part->sb->inode_bitmap_lba + off_sec;
			bitmap_off = part->inode_bitmap.bits + off_size;
			break;
		case BLOCK_BITMAP:
			sec_lba = part->sb->block_bitmap_lba + off_sec;
			bitmap_off = part->block_bitmap.bits + off_size;
			break;
	}
	ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}

/* 创建文件，若成功则返回文件描述符，否则返回-1 */
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag) {
	/* 分配后续操作的缓冲区 */
	void* io_buf = sys_malloc(1024);
	if (io_buf == NULL) {
		printk("in file_create: sys_malloc for io_buf failed\n");
		return -1;
	}

	/* 用于操作失败时回滚各资源状态 */
	uint8_t rollback_step = 0;

	/* 为新文件分配inode */
	int32_t inode_no = inode_bitmap_alloc(cur_part);
	if (inode_no == -1) {
		printk("in file_create: allocate inode failed\n");
		return -1;
	}

	/* 此处inode要从堆中申请内存，不可使用局部变量（栈中，函数退出后释放），*/
	/* 因为file_table数组中的元素file的inode指针要指向它 */
	struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode));
	if (new_file_inode == NULL) {
		printk("file_create: sys_malloc for inode failed\n");
		rollback_step = 1;
		goto rollback;
	}
	inode_init(inode_no, new_file_inode);    // 初始化inode结点

	/* 从打开文件表file_table获取一个空闲位 */
	int fd_idx = get_free_slot_in_global();
	if (fd_idx == -1) {
		printk("exceed max open files\n");
		rollback_step = 2;
		goto rollback;
	}
	file_table[fd_idx].fd_inode = new_file_inode;
	file_table[fd_idx].fd_pos = 0;
	file_table[fd_idx].fd_flag = flag;
	file_table[fd_idx].fd_inode->write_deny = false;

	/* 创建目录项 */
	struct dir_entry new_dir_entry;
	memset(&new_dir_entry, 0, sizeof(struct dir_entry));
	create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);

	/* 同步内存数据到硬盘 */
	
	/* 1. 在目录parent_dir下安装目录项new_dir_entry，成功写入硬盘返回true，否则返回false */
	if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
		printk("sync dir_entry to disk failed\n");
		rollback_step = 3;
		goto rollback;
	}

	/* 2. 将父目录inode结点的内容同步到硬盘 */
	memset(io_buf, 0, 1024);
	inode_sync(cur_part, parent_dir->inode, io_buf);

	/* 3. 将新创建文件的inode结点的内容同步到硬盘 */
	memset(io_buf, 0, 1024);
	inode_sync(cur_part, new_file_inode, io_buf);

	/* 4. 将inode_bitmap位图同步到硬盘 */
	bitmap_sync(cur_part, inode_no, INODE_BITMAP);

	/* 5. 将创建的文件inode结点添加到open_inodes链表 */
	list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
	new_file_inode->i_open_cnts = 1;

	sys_free(io_buf);
	return pcb_fd_install(fd_idx);

/* 创建文件过程中需要创建相关的多个资源，若其中某步失败则会执行下面的回滚步骤 */
rollback:
	switch (rollback_step) {
		case 3:
			/* 失败时，将file_table中相应位清空 */
			memset(&file_table[fd_idx], 0, sizeof(struct file));
		case 2:
			sys_free(new_file_inode);
		case 1:
			/* 如果新文件的inode结点内存申请失败，则之前位图中分配的inode_no也要恢复 */
			bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
			break;
	}

	sys_free(io_buf);
	return -1;
}

