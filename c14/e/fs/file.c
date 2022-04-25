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

/* 打开编号为inode_no的inode对应文件，若成功返回文件描述符，否则返回-1 */
int32_t file_open(uint32_t inode_no, uint8_t flag) {
	int fd_idx = get_free_slot_in_global();
	if (fd_idx == -1) {
		printk("exceed max open files\n");
		return -1;
	}
	file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
	/* 每次打开文件，要将fd_pos置为0，即让文件指针指向开头 */
	file_table[fd_idx].fd_pos = 0;
	file_table[fd_idx].fd_flag = flag;
	/* 这里使用指针 */
	bool* write_deny = &file_table[fd_idx].fd_inode->write_deny;

	/* 如果与写文件相关，判断是否有其他进程正在写文件 */
	if (flag & O_WRONLY || flag & O_RDWR) {
		enum intr_status old_status = intr_disable();    // 进入临界区前，关中断
		/* 若当前没有其他进程写文件，将其占用 */
		if (!(*write_deny)) {
			*write_deny = true;
			intr_set_status(old_status);             // 恢复中断
		/* 失败返回 */
		} else {
			intr_set_status(old_status);
			printk("file can not be write now, try again later\n");
			return -1;
		}
	}

	/* 若是读文件或创建文件，不需要处理write_deny */
	return pcb_fd_install(fd_idx);
}

/* 关闭文件 */
int32_t file_close(struct file* file) {
	if (file == NULL) {
		return -1;
	}
	file->fd_inode->write_deny = false;
	inode_close(file->fd_inode);
	file->fd_inode = NULL;    // 使文件结构可用
	return 0;
}

/* 把buf中的count个字节写入file，成功则返回写入的字节数，失败则返回-1 */
int32_t file_write(struct file* file, const void* buf, uint32_t count) {
	/* 文件目前最大支持140 * 512 = 71680字节 */
	if (file->fd_inode->i_size + count > BLOCK_SIZE * 140) {
		printk("exceed max file_size 71680 bytes, write file failed\n");
		return -1;
	}

	uint8_t* io_buf = sys_malloc(BLOCK_SIZE);
	if (io_buf == NULL) {
		printk("file_write: sys_malloc for io_buf failed\n");
		return -1;
	}

	/* 用来记录文件所有的块地址 */
	uint32_t* all_blocks = (uint32_t*)sys_malloc(48 + 512);
	if (all_blocks == NULL) {
		printk("file_write: sys_malloc for all_block failed\n");
		return -1;
	}

	const uint8_t* src = buf;         // src指向buf中待写入的数据
	uint32_t bytes_written = 0;       // 用来记录已写入数据字节数
	uint32_t size_left = count;       // 用来记录未写入数据字节数
	int32_t block_lba = -1;           // 块地址
	uint32_t block_bitmap_idx = 0;    // 用来记录block对应于block_bitmap中的索引，作为参数传给bitmap_sync
	uint32_t sec_idx;                 // 用来索引扇区
	uint32_t sec_lba;                 // 扇区地址
	uint32_t sec_off_bytes;           // 扇区内字节偏移量
	uint32_t sec_left_bytes;          // 扇区内剩余字节量
	uint32_t chunk_size;              // 每次写入硬盘的数据大小
	int32_t indirect_block_table;     // 用来获取一级间接块表地址
	uint32_t block_idx;               // 块索引

	/* 判断文件是否是第一次写，如果是，先为其分配一个块 */
	if (file->fd_inode->i_sectors[0] == 0) {
		block_lba = block_bitmap_alloc(cur_part);
		if (block_lba == -1) {
			printk("file_write: block_bitmap_alloc failed\n");
			return -1;
		}
		file->fd_inode->i_sectors[0] = block_lba;

		/* 每分配一个块就将块位图同步到硬盘 */
		block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
		ASSERT(block_bitmap_idx != 0);
		bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
	}

	/* 写入count字节前，该文件已经占用的块数目 */
	uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;

	/* 写入count字节后，该文件将占用的块数目 */
	uint32_t file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
	ASSERT(file_will_use_blocks <= 140);

	/* 通过块增量判断是否需要分配扇区，如果块增量为0，说明不需要分配 */
	uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;

	/* 开始将文件所有块地址收集到all_blocks，后面统一在all_blocks中获取待写入扇区地址 */
	if (add_blocks == 0) {    // 不涉及到新扇区分配
		/* 文件数据量在12块以内，不涉及到间接块 */
		if (file_has_used_blocks <= 12) {
			block_idx = file_has_used_blocks - 1;    // 指向最后一个有数据的扇区
			all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
		/* 在未写入新数据之前已经占用了间接块，需要将间接块地址读进来 */
		} else {
			ASSERT(file->fd_inode->i_sectors[12] != 0);
			indirect_block_table = file->fd_inode->i_sectors[12];
			ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
		}
	} else {                  // 若涉及新扇区分配及一级间接块表分配，分三种情况处理
		/* 情况一，12个直接块够用 */
		if (file_will_use_blocks <= 12) {
			/* 先将有剩余空间的可用扇区地址写入all_blocks */
			block_idx = file_has_used_blocks - 1;
			ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
			all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

			/* 再将未来要用的扇区分配好后写入all_blocks */
			block_idx = file_has_used_blocks;    // block_idx现在指向第一个要分配的新扇区
			while (block_idx < file_will_use_blocks) {
				block_lba = block_bitmap_alloc(cur_part);
				if (block_lba == -1) {
					printk("file_write: block_bitmap_alloc for situation 1 failed\n");
					return -1;
				}

				/* 注意，在写文件时，不应该存在块未使用但已经分配扇区的情况，当文件删除时会将块地址清0 */
				ASSERT(file->fd_inode->i_sectors[block_idx] == 0);    // 确保尚未分配扇区地址

				file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;

				/* 每分配一个块就将块位图同步到硬盘 */
				block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
				bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

				block_idx++;    // 处理下一个要分配的新扇区
			}
		/* 情况二，旧数据在12个直接块内，新数据需要用到间接块 */
		} else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12) {
			/* 先将有剩余空间的可用扇区地址写入all_blocks */
			block_idx = file_has_used_blocks - 1;
			all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

			/* 创建一级间接块表 */
			block_lba = block_bitmap_alloc(cur_part);
			if (block_lba == -1) {
				printk("file_write: block_bitmap_alloc for situation 2 failed\n");
				return -1;
			}
			ASSERT(file->fd_inode->i_sectors[12] == 0);                          // 确保一级间接块表未分配
			indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;    // 分配一级间接块表

			/* 再将未来要用的扇区分配好后写入all_blocks */
			block_idx = file_has_used_blocks;
			while (block_idx < file_will_use_blocks) {
				block_lba = block_bitmap_alloc(cur_part);
				if (block_lba == -1) {
					printk("file_write: block_bitmap_alloc for situation 2 failed\n");
					return -1;
				}
				
				if (block_idx < 12) {
					ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
					file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
				} else {
					all_blocks[block_idx] = block_lba;    // 间接块全部分配完成后一次性同步到硬盘
				}
				
				/* 每分配一个块就将块位图同步到硬盘 */
				block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
				bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

				block_idx++;    // 处理下一个要分配的新扇区
			}

			/* 将一级间接块表同步到硬盘 */
			ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);

		/* 情况三，旧数据已经用到间接块 */
		} else if (file_has_used_blocks > 12) {
			ASSERT(file->fd_inode->i_sectors[12] != 0);    // 确保一级间接块表已经分配
			indirect_block_table = file->fd_inode->i_sectors[12];

			/* 获取所有间接块地址 */
			ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);

			block_idx = file_has_used_blocks;    // block_idx指向第一个需要分配的间接块
			while (block_idx < file_will_use_blocks) {
				block_lba = block_bitmap_alloc(cur_part);
				printk("file_write: block_bitmap_alloc for situation 3 failed\n");
				return -1;
			}
			all_blocks[block_idx++] = block_lba;

			/* 每分配一个块就将块位图同步到硬盘 */
			block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
			bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

			/* 将一级间接块表同步到硬盘 */
			ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
		}
	}

	/* 执行到这一步，所有要用到的块地址已经收集到all_blocks中，下面开始写数据 */
	bool first_write_block = true;                // 标识第一个写入的块，需要先读取，拼接后写入
	file->fd_pos = file->fd_inode->i_size - 1;    // 置fd_pos为文件大小-1，下面在写数据时随时更新
	/* 直到写完所有数据 */
	while (bytes_written < count) {
		memset(io_buf, 0, BLOCK_SIZE);
		sec_idx = file->fd_inode->i_size / BLOCK_SIZE;
		sec_lba = all_blocks[sec_idx];
		sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;
		sec_left_bytes = BLOCK_SIZE - sec_off_bytes;

		/* 判断此次写入硬盘的数据大小 */
		chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
		if (first_write_block) {
			ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
			first_write_block = false;
		}
		memcpy(io_buf + sec_off_bytes, src, chunk_size);
		ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
		printk("file write at lba 0x%x\n", sec_lba);

		src += chunk_size;                       // 指针移动chunk_size
		file->fd_inode->i_size += chunk_size;    // 更新文件大小
		file->fd_pos += chunk_size;              // fd_pos移动chunk_size
		bytes_written += chunk_size;
		size_left -= chunk_size;
	}

	/* 同步当前文件的inode（更新后）到硬盘 */
	inode_sync(cur_part, file->fd_inode, io_buf);

	sys_free(all_blocks);
	sys_free(io_buf);
	return bytes_written;
}

