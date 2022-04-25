#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "stdio_kernel.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"

/* 根目录 */
struct dir root_dir;

/* 打开根目录 */
void open_root_dir(struct partition* part) {
	root_dir.inode = inode_open(part, part->sb->root_inode_no);
	root_dir.dir_pos = 0;
}

/* 打开分区part上inode_no号inode目录并返回目录指针 */
struct dir* dir_open(struct partition* part, uint32_t inode_no) {
	struct dir* pdir = (struct dir*)sys_malloc(sizeof(struct dir));
	pdir->inode = inode_open(part, inode_no);
	pdir->dir_pos = 0;
	return pdir;
}

/* 关闭目录 */
void dir_close(struct dir* dir) {
	/* 根目录打开后不关闭 */
	if (dir == &root_dir) {
		return;
	}
	inode_close(dir->inode);
	sys_free(dir);
}

/* 在part分区的pdir目录内寻找名为name的文件或目录，找到则返回true并将其目录项存入dir_e，没找到则返回false */
bool search_dir_entry(struct partition* part, struct dir* pdir, const char* name, struct dir_entry* dir_e) {
	uint32_t block_cnt = 140;    // 12个直接块，128个一级间接块
	uint32_t* all_blocks = (uint32_t*)sys_malloc(48 + 512);
	if (all_blocks == NULL) {
		printk("search_dir_entry: sys_malloc for all_blocks failed");
		return false;
	}

	uint32_t block_idx = 0;
	while (block_idx < 12) {
		all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
		block_idx++;
	}

	block_idx = 0;
	if (pdir->inode->i_sectors[12] != 0) {    // 若含有一级间接块表
		ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
	}
	/* 至此，all_blocks存储该文件或目录内容的所有扇区号 */

	/* 写目录项时保证目录项不跨扇区，这样方便读取目录项 */
	uint8_t* buf = (uint8_t*)sys_malloc(SECTOR_SIZE);
	struct dir_entry* p_de = (struct dir_entry*)buf;          // p_de是指向目录项的指针，值为buf起始地址
	uint32_t dir_entry_size = part->sb->dir_entry_size;
	uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;    // 1扇区可容纳的目录项数目

	/* 开始在所有块中查找目录项 */
	while (block_idx < block_cnt) {
		/* 块地址为0说明该块无数据，继续在其他块中查找 */
		if (all_blocks[block_idx] == 0) {
			block_idx++;
			continue;
		}
		ide_read(part->my_disk, all_blocks[block_idx], buf, 1);

		uint32_t dir_entry_idx = 0;
		/* 遍历该扇区下所有目录项 */
		while (dir_entry_idx < dir_entry_cnt) {
			/* 找到了，复制整个目录项 */
			if (!strcmp(p_de->filename, name)) {
				memcpy(dir_e, p_de, dir_entry_size);
				sys_free(buf);
				sys_free(all_blocks);
				return true;
			}
			dir_entry_idx++;
			p_de++;
		}
		block_idx++;
		p_de = (struct dir_entry*)buf;    // p_de重新赋值为buf
		memset(buf, 0, SECTOR_SIZE);      // 将buf清0
	}
	sys_free(buf);
	sys_free(all_blocks);
	return false;
}

/* 在内存中初始化目录项p_de */
void create_dir_entry(char* filename, uint32_t inode_no, uint8_t file_type, struct dir_entry* p_de) {
	ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);

	memcpy(p_de->filename, filename, strlen(filename));
	p_de->i_no = inode_no;
	p_de->f_type = file_type;
}

/* 将目录项p_de写入父目录parent_dir中，io_buf由主调函数提供 */
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf) {
	struct inode* dir_inode = parent_dir->inode;
	uint32_t dir_size = dir_inode->i_size;
	uint32_t dir_entry_size = cur_part->sb->dir_entry_size;

	ASSERT(dir_size % dir_entry_size == 0);

	uint32_t dir_entrys_per_sec = (512 / dir_entry_size);    // 每个扇区最大目录项数
	int32_t block_lba = -1;

	/* 将该目录所有扇区地址存入all_blocks */
	uint8_t block_idx = 0;
	uint32_t all_blocks[140] = {0};

	/* 将12个直接块存入all_blocks */
	while (block_idx < 12) {
		all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
		block_idx++;
	}

	struct dir_entry* dir_e = (struct dir_entry*)io_buf;     // dir_e用于在io_buf中遍历目录项
	int32_t block_bitmap_idx = -1;

	/* 开始遍历所有块以寻找目录项空位，若已有扇区中没有空位，则申请新扇区以存储该目录项 */
	block_idx = 0;
	while (block_idx < 140) {    // 文件（包括目录）最大支持12个直接块和128个间接块
		block_bitmap_idx = -1;
		/* 若第block_idx块不存在，分配新块 */
		if (all_blocks[block_idx] == 0) { 
			block_lba = block_bitmap_alloc(cur_part);
			if (block_lba == -1) {
				printk("alloc block bitmap for sync_dir_entry failed\n");
				return false;
			}

			/* 每分配一个块都同步一次block_bitmap */
			block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
			ASSERT(block_bitmap_idx != -1);
			bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

			block_bitmap_idx = -1;
			if (block_idx < 12) {            // 若是直接块未分配
				dir_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
			} else if (block_idx == 12) {    // 若是一级间接块表未分配
				dir_inode->i_sectors[12] = block_lba;        // 将上面分配的块作为一级间接块表
				block_lba = -1;
				block_lba = block_bitmap_alloc(cur_part);    // 再分配一个块作为第0个间接块
				if (block_lba == -1) {
					block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
					bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
					dir_inode->i_sectors[12] = 0;
					printk("alloc block bitmap for sync_dir_entry failed\n");
					return false;
				}

				/* 每分配一个块都同步一次block_bitmap */
				block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
				ASSERT(block_bitmap_idx != -1);
				bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

				all_blocks[12] = block_lba;
				/* 把新分配的第0个间接块地址写入一级间接块表 */
				ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
			} else {                         // 若是间接块未分配
				all_blocks[block_idx] = block_lba;
				/* 把新分配的第block_idx - 12个间接块地址写入一级间接块表 */
				ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
			}

			/* 再将新目录项p_de写入新分配的块 */
			memset(io_buf, 0, 512);
			memcpy(io_buf, p_de, dir_entry_size);
			ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
			dir_inode->i_size += dir_entry_size;
			return true;
		}

		/* 若第block_idx块已存在，将其读进内存，在该块中查找空目录项 */
		ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
		/* 在扇区内查找空目录项 */
		uint8_t dir_entry_idx = 0;
		while (dir_entry_idx < dir_entrys_per_sec) {
			/* 初始化或删除文件后，将f_type置为FT_UNKNOWN */
			if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) {
				memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
				ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
				dir_inode->i_size += dir_entry_size;
				return true;
			}
			dir_entry_idx++;
		}

		block_idx++;
	}

	printk("directory is full!\n");
	return false;
}

/* 把分区part的pdir目录中编号为inode_no的目录项删除 */
bool delete_dir_entry(struct partition* part, struct dir* pdir, uint32_t inode_no, void* io_buf) {
	struct inode* dir_inode = pdir->inode;
	uint32_t block_idx = 0, all_blocks[140] = {0};

	/* 收集目录全部块地址 */
	while (block_idx < 12) {
		all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
		block_idx++;
	}
	if (dir_inode->i_sectors[12]) {
		ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
	}

	/* 目录项在存储时保证不会跨扇区 */
	uint32_t dir_entry_size = part->sb->dir_entry_size;
	uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size);    // 每扇区最大目录项数目
	struct dir_entry* dir_e = (struct dir_entry*)io_buf;
	struct dir_entry* dir_entry_found = NULL;
	uint8_t dir_entry_idx, dir_entry_cnt;
	bool is_dir_first_block = false;                                 // 目录的第一个块

	/* 遍历所有块，寻找目录项 */
	block_idx = 0;
	while (block_idx < 140) {
		is_dir_first_block = false;
		if (all_blocks[block_idx] == 0) {
			block_idx++;
			continue;
		}

		dir_entry_idx = dir_entry_cnt = 0;
		memset(io_buf, 0, SECTOR_SIZE);
		/* 读取扇区，获得目录项 */
		ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);

		/* 遍历所有目录项，统计该扇区的目录项数量以及是否有待删除的目录项 */
		while (dir_entry_idx < dir_entrys_per_sec) {
			if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
				if (!strcmp((dir_e + dir_entry_idx)->filename, ".")) {
					is_dir_first_block = true;
				} else if (strcmp((dir_e + dir_entry_idx)->filename, ".") && strcmp((dir_e + dir_entry_idx)->filename, "..")) {
					dir_entry_cnt++;    // 统计此扇区内目录项个数，用于判断删除目录项后是否回收该扇区
					/* 找到了inode_no结点 */
					if ((dir_e + dir_entry_idx)->i_no == inode_no) {
						ASSERT(dir_entry_found == NULL);
						dir_entry_found = dir_e + dir_entry_idx;
					}
					/* 找到后继续遍历，统计总目录项数量 */
				}
			}
			dir_entry_idx++;
		}

		/* 若本扇区未找到，继续在下一扇区中查找 */
		if (dir_entry_found == NULL) {
			block_idx++;
			continue;
		}

		/* 若在本扇区找到目录项，则清除该目录项并判断是否需要回收扇区，随后退出循环直接返回 */
		ASSERT(dir_entry_cnt >= 1);

		/* 除了目录的第一个扇区以外，若本扇区只有该目录项自己，则回收本扇区 */
		if (dir_entry_cnt == 1 && !is_dir_first_block) {
			/* 1. 在块位图中回收该块 */
			uint32_t block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
			bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
			bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

			/* 2. 将数组i_sectors或间接块表中的块地址清0 */
			if (block_idx < 12) {    // 数组i_sectors
				dir_inode->i_sectors[block_idx] = 0;
			} else {                 // 间接块表
				/* 先判断间接块表中间接块数量，若仅有这一个间接块，则连同间接块表的扇区一起回收 */
				uint32_t indirect_blocks = 0;
				uint32_t indirect_block_idx = 12;
				while (indirect_block_idx < 140) {
					if (all_blocks[indirect_block_idx] != 0) {
						indirect_blocks++;
					}
				}
				ASSERT(indirect_blocks >= 1);

				/* 间接块表中还包括其他间接块，则仅在间接块表中擦除当前间接块地址 */
				if (indirect_blocks > 1) {
					all_blocks[block_idx] = 0;
					ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
				/* 间接块表中只有当前间接块，则直接把间接块表所在块回收，并擦除间接块表地址 */
				} else {
					block_bitmap_idx = dir_inode->i_sectors[12] - part->sb->data_start_lba;
					bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
					bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

					dir_inode->i_sectors[12] = 0;
				}
			}
		/* 仅将该目录项清除 */
		} else {
			memset(dir_entry_found, 0, dir_entry_size);
			ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
		}

		/* 更新目录inode信息并同步到硬盘 */
		ASSERT(dir_inode->i_size >= dir_entry_size);
		dir_inode->i_size -= dir_entry_size;
		memset(io_buf, 0, SECTOR_SIZE * 2);
		inode_sync(part, dir_inode, io_buf);

		return true;
	}

	/* 在目录所有块中都未找到该目录项，返回false，出现这种情况应该是search_file出错 */
	return false;
}

/* 读取目录，成功则返回1个目录项，失败返回NULL */
struct dir_entry* dir_read(struct dir* dir) {
	struct dir_entry* dir_e = (struct dir_entry*)dir->dir_buf;    // 指向目录项缓存

	/* 收集目录所有块地址 */
	struct inode* dir_inode = dir->inode;
	uint32_t all_blocks[140] = {0}, block_cnt = 12;
	uint32_t block_idx = 0;
	while (block_idx < 12) {
		all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
		block_idx++;
	}
	if (dir_inode->i_sectors[12] != 0) {
		ide_read(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
		block_cnt = 140;
	}
	block_idx = 0;

	uint32_t dir_entry_idx = 0;
	uint32_t cur_dir_entry_pos = 0;    // 当前目录项的偏移
	uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
	uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;

	/* 遍历所有块，因为可能有空块 */
	while (block_idx < block_cnt) {
		/* 读取范围超出目录项总大小 */
		if (dir->dir_pos >= dir_inode->i_size) {
			return NULL;
		}

		/* 块地址为0，即空块，读取下一个块 */
		if (all_blocks[block_idx] == 0) {
			block_idx++;
			continue;
		}

		/* 读取一个扇区的目录项到缓存 */
		memset(dir_e, 0, SECTOR_SIZE);
		ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);
		/* 遍历扇区内所有目录项 */
		dir_entry_idx = 0;
		while (dir_entry_idx < dir_entrys_per_sec) {
			/* 若f_type不等于0，即不等于FT_UNKNOWN，说明为有效目录项 */
			if ((dir_e + dir_entry_idx)->f_type) {
				/* 跳过已经返回的目录项 */
				if (cur_dir_entry_pos < dir->dir_pos) {
					cur_dir_entry_pos += dir_entry_size;
					dir_entry_idx++;
					continue;
				}

				ASSERT(cur_dir_entry_pos == dir->dir_pos);
				/* 每返回一个目录项使dir_pos增加一个目录项大小 */
				dir->dir_pos += dir_entry_size;
				/* 此处需要区分索引与偏移，偏移不包含无效目录项 */
				return dir_e + dir_entry_idx;
			}
			dir_entry_idx++;
		}
		block_idx++;
	}

	return NULL;
}
