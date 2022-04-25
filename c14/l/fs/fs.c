#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio_kernel.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "file.h"
#include "console.h"

/* 当前操作的分区 */
struct partition* cur_part;

/* 在分区链表中找到名为part_name的分区，并将其指针赋值给cur_part */
static bool mount_partition(struct list_elem* pelem, int arg) {
	char* part_name = (char*)arg;
	struct partition* part = elem2entry(struct partition, part_tag, pelem);
	
	if (!strcmp(part->name, part_name)) {
		cur_part = part;
		struct disk* hd = cur_part->my_disk;

		/* sb_buf用来存储从硬盘上读入的超级块 */
		struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

		/* 在内存中创建分区cur_part的超级块 */
		cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
		if (cur_part->sb == NULL) {
			PANIC("alloc memory failed!");
		}

		/* 读入超级块 */
		memset(sb_buf, 0, SECTOR_SIZE);
		ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);

		/* 把sb_buf中超级块的信息复制到分区的超级块sb中 */
		memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

		/* 把硬盘上的块位图读入到内存 */
		cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
		if (cur_part->block_bitmap.bits == NULL) {
			PANIC("alloc memory failed!");
		}
		cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;

		ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);

		/* 把硬盘上的inode位图读入到内存 */
		cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
		if (cur_part->inode_bitmap.bits == NULL) {
			PANIC("alloc memory failed!");
		}
		cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;

		ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);

		/* 初始化当前操作分区打开inode队列 */
		list_init(&cur_part->open_inodes);

		sys_free(sb_buf);
		printk("mount %s done!\n", part->name);

		return true;
	}

	/* 使list_traversal继续遍历 */
	return false;
}

/* 格式化分区，也就是初始化分区的元信息，创建文件系统 */
static void partition_format(struct partition* part) {
	/* 简单起见，一个块大小为一个扇区 */
	uint32_t boot_sector_sects = 1;
	uint32_t super_block_sects = 1;
	/* inode位图占用的扇区数，一个分区最多支持4096个文件 */
	uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
	/* inode数组占用的扇区数 */
	uint32_t inode_table_sects = DIV_ROUND_UP((sizeof(struct inode) * MAX_FILES_PER_PART), SECTOR_SIZE);

	uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
	uint32_t free_sects = part->sec_cnt - used_sects;

	/* 处理块位图占用的扇区数 */
	uint32_t block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
	/* block_bitmap_bit_len是块位图中位个数，也是可用块的数量 */
	uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
	block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

	/* 超级块初始化 */
	struct super_block sb;
	sb.magic = 0x19590318;
	sb.sec_cnt = part->sec_cnt;
	sb.inode_cnt = MAX_FILES_PER_PART;
	sb.part_lba_base = part->start_lba;
	
	sb.block_bitmap_lba = sb.part_lba_base + 2;    // 第0块是操作系统引导块，第1块是超级块
	sb.block_bitmap_sects = block_bitmap_sects;

	sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
	sb.inode_bitmap_sects = inode_bitmap_sects;

	sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
	sb.inode_table_sects = inode_table_sects;

	sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
	sb.root_inode_no = 0;    // inode数组中第0个inode预留给根目录
	sb.dir_entry_size = sizeof(struct dir_entry);

	printk("%s info:\n", part->name);
	printk("   magic:0x%x\n   part_lba_base:0x%x\n   all_sectors:0x%x\n   inode_cnt:0x%x\n   block_bitmap_lba:0x%x\n   block_bitmap_sectors:0x%x\n   inode_bitmap_lba:0x%x\n   inode_bitmap_sectors:0x%x\n   inode_table_lba:0x%x\n   inode_table_sectors:0x%x\n   data_start_lba:0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);

	/* 1. 将超级块写入本分区的1扇区 */
	struct disk* hd = part->my_disk;
	ide_write(hd, part->start_lba + 1, &sb, 1);
	printk("    super_block_lba:0x%x\n", part->start_lba + 1);

	/* 找出数据量最大的元信息，用其尺寸分配存储缓冲区 */
	uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
	buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
	uint8_t* buf = (uint8_t*)sys_malloc(buf_size);    // 申请的内存由内存管理系统清0后返回

	/* 2. 将块位图初始化并写入sb.block_bitmap_lba */
	buf[0] |= 0x01;    // 第0个块预留给根目录，块位图中占位

	uint32_t block_bitmap_last_byte  = block_bitmap_bit_len / 8;    // 接下来把块位图最后一个扇区中不属于空闲块的位置1
	uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
	uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);    // last_size是块位图所在最后一个扇区中，不足一扇区的其他部分

	/* 先将块位图最后一字节到其所在扇区的结束全置为1 */
	memset(&buf[block_bitmap_last_byte], 0xff, last_size);

	/* 再将上一步中覆盖的最后一字节内的有效位恢复为0 */
	uint8_t bit_idx = 0;
	while (bit_idx <= block_bitmap_last_bit) {
		buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
	}

	ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

	/* 3. 将inode位图初始化并写入sb.inode_bitmap_lba */
	/* 先清空存储缓冲区 */
	memset(buf, 0, buf_size);
	buf[0] |= 0x01;    // 第0个inode预留给了根目录，inode位图中占位

	/* 由于inode_table中共4096个inode，位图inode_bitmap正好占用1个扇区，即inode_bitmap_sects等于1，所以位图中的位 */
	/* 全都代表inode_table中的inode，没有多余的无效位，无需再像block_bitmap那样单独处理最后一个扇区的剩余部分 */
	ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

	/* 4. 将inode数组初始化并写入sb.inode_table_lba */
	/* 准备inode_table中的第0项，即根目录所在的inode */
	memset(buf, 0, buf_size);
	struct inode* i = (struct inode*)buf;
	i->i_size = sb.dir_entry_size * 2;      // .和..的目录项
	i->i_no = 0;                            // 根目录占inode数组中第0个inode
	i->i_sectors[0] = sb.data_start_lba;    // i_sectors数组的其他元素都被初始化为0

	ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

	/* 5. 将根目录写入sb.data_start_lba */
	/* 写入根目录的两个目录项.和.. */
	memset(buf, 0, buf_size);
	struct dir_entry* p_de = (struct dir_entry*)buf;

	/* 初始化当前目录. */
	memcpy(p_de->filename, ".", 1);
	p_de->i_no = 0;
	p_de->f_type = FT_DIRECTORY;
	p_de++;

	/* 初始化当前目录父目录.. */
	memcpy(p_de->filename, "..", 2);
	p_de->i_no = 0;    // 根目录的父目录仍然是根目录自己
	p_de->f_type = FT_DIRECTORY;

	/* sb.data_start_lba已经分配给了根目录，里面是根目录的目录项 */
	ide_write(hd, sb.data_start_lba, buf, 1);

	printk("    root_dir_lba:0x%x\n", sb.data_start_lba);
	printk("%s format done\n", part->name);
	sys_free(buf);
}

/* 将最上层路径名称解析出来 */
static char* path_parse(char* pathname, char* name_store) {
	if (pathname[0] == '/') {
		while (*(++pathname) == '/');
	}

	while (*pathname != '/' && *pathname != 0) {
		*name_store++ = *pathname++;
	}

	if (pathname[0] == 0) {
		return NULL;
	}
	/* 返回子路径 */
	return pathname;
}

/* 返回路径深度，如/a/b/c，深度为3 */
int32_t path_depth_cnt(char* pathname) {
	ASSERT(pathname != NULL);

	char* p = pathname;
	char name[MAX_FILE_NAME_LEN];
	uint32_t depth = 0;

	/* 解析路径，从中拆分出各级名称 */
	p = path_parse(p, name);
	while (name[0]) {
		depth++;
		memset(name, 0, MAX_FILE_NAME_LEN);
		if (p) {    // 逐级解析
			p = path_parse(p, name);
		}
	}
	return depth;
}

/* 搜索文件pathname，若找到则返回其inode号，否则返回-1 */
static int search_file(const char* pathname, struct path_search_record* searched_record) {
	/* 若查找的是根目录，直接返回根目录信息 */
	if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
		searched_record->parent_dir = &root_dir;
		searched_record->file_type = FT_DIRECTORY;
		searched_record->searched_path[0] = 0;    // 搜索路径置空
		return 0;    // 返回根目录inode
	}

	uint32_t path_len = strlen(pathname);
	ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);
	
	char* sub_path = (char*)pathname;
	struct dir* parent_dir = &root_dir;
	struct dir_entry dir_e;

	/* 记录路径解析出来的各级名称，如路径a/b/c，则数组name依次被赋值为"a"，"b"，"c" */
	char name[MAX_FILE_NAME_LEN] = {0};

	searched_record->parent_dir = parent_dir;
	searched_record->file_type = FT_UNKNOWN;
	uint32_t parent_inode_no = 0;    // 父目录的inode号

	sub_path = path_parse(sub_path, name);
	while (name[0]) {    // 查找结束
		ASSERT(strlen(searched_record->searched_path) < 512);

		/* 记录已存在的父目录 */
		strcat(searched_record->searched_path, "/");
		strcat(searched_record->searched_path, name);

		/* 在给定目录parent_dir中查找文件 */
		if (search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
			memset(name, 0, MAX_FILE_NAME_LEN);

			/* 若未结束，继续拆分路径 */
			if (sub_path) {
				sub_path = path_parse(sub_path, name);
			}

			if (FT_DIRECTORY == dir_e.f_type) {         // 如果找到的是目录
				parent_inode_no = parent_dir->inode->i_no;
				dir_close(parent_dir);
				parent_dir = dir_open(cur_part, dir_e.i_no);    // 更新父目录
				searched_record->parent_dir = parent_dir;
				continue;
			} else if (FT_REGULAR == dir_e.f_type) {    // 如果找到的是普通文件
				searched_record->file_type = FT_REGULAR;
				return dir_e.i_no;
			}
		} else {    // 找不到文件
			/* 找不到目录项时，要留着parent_dir不要关闭，若是创建新文件的话需要在parent_dir中创建 */
			return -1;
		}
	}

	/* 执行到此，必然是遍历了完整路径，并且要查找的文件或目录只有同名目录存在 */
	dir_close(searched_record->parent_dir);

	/* 保存被查找目录的直接父目录 */
	searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
	searched_record->file_type = FT_DIRECTORY;
	return dir_e.i_no;
}

/* 打开或创建文件成功后，返回文件描述符，否则返回-1 */
int32_t sys_open(const char* pathname, uint8_t flags) {
	/* 打开目录使用dir_open */
	if (pathname[strlen(pathname) - 1] == '/') {
		printk("can not open a directory %s\n", pathname);
		return -1;
	}
	ASSERT(flags <= 7);

	int32_t fd = -1;    // 默认为找不到文件

	struct path_search_record searched_record;
	memset(&searched_record, 0, sizeof(struct path_search_record));

	/* 记录目录深度，帮助判断中间某个目录不存在的情况 */
	uint32_t pathname_depth = path_depth_cnt((char*)pathname);

	/* 先检查文件是否存在 */
	int inode_no = search_file(pathname, &searched_record);
	bool found = inode_no != -1 ? true : false;

	if (searched_record.file_type == FT_DIRECTORY) {
		printk("can not open a directory with open(), use dir_open() to instead\n");
		dir_close(searched_record.parent_dir);
		return -1;
	}

	uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

	/* 先判断是否把pathname的各层目录都访问到了，即是否在某个中间目录就失败了 */
	if (pathname_depth != path_searched_depth) {
		printk("can not access %s: Not a directory, subpath %s is not exist\n", pathname, searched_record.searched_path);
		dir_close(searched_record.parent_dir);
		return -1;
	}

	/* 若是在最后一个路径上没找到，并且并不是要创建文件，直接返回-1 */
	if (!found && !(flags & O_CREAT)) {
		printk("in path %s, file %s is not exist\n", searched_record.searched_path, (strrchr(searched_record.searched_path, '/') + 1));
		dir_close(searched_record.parent_dir);
		return -1;
	/* 若要创建的文件已存在 */
	} else if (found && flags & O_CREAT) {
		printk("%s has already exist!\n", pathname);
		dir_close(searched_record.parent_dir);
		return -1;
	}

	switch (flags & O_CREAT) {
		case O_CREAT:
			printk("creating file\n");
			fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
			dir_close(searched_record.parent_dir);
			break;
		/* 其余为打开文件：O_RDONLY、O_WRONLY、O_RDWR */
		default:
			fd = file_open(inode_no, flags);
	}

	/* 返回文件描述符fd，即任务pcb->fd_table数组中的元素下标 */
	return fd;
}

/* 将文件描述符转化为打开文件表下标 */
static uint32_t fd_local2global(uint32_t local_fd) {
	struct task_struct* cur = running_thread();
	int32_t global_fd = cur->fd_table[local_fd];

	ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
	return (uint32_t)global_fd;
}

/* 关闭文件描述符fd指向的文件，成功返回0，失败返回-1 */
int32_t sys_close(int32_t fd) {
	int32_t ret = -1;
	if (fd > 2) {
		uint32_t fd_ = fd_local2global(fd);
		ret = file_close(&file_table[fd_]);
		running_thread()->fd_table[fd] = -1;    // 使该文件描述符可用
	}
	return ret;
}

/* 将buf中连续count个字节写入文件描述符fd，成功则返回写入的字节数，失败返回-1 */
int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
	if (fd < 0) {
		printk("sys_write: fd error\n");
		return -1;
	}

	if (fd == stdout_no) {
		char tmp_buf[1024] = {0};
		memcpy(tmp_buf, buf, count);
		console_put_str(tmp_buf);
		return count;
	}

	uint32_t fd_ = fd_local2global(fd);
	struct file* wr_file = &file_table[fd_];
	if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
		uint32_t bytes_written = file_write(wr_file, buf, count);
		return bytes_written;
	} else {
		printk("sys_write: not allowed to write file without flag O_RDWR OR O_WRONLY\n");
		return -1;
	}
}

/* 从文件描述符fd指向的文件中读取count个字节到buf，若成功返回读取的字节数，到文件尾返回-1 */
int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
	if (fd < 0) {
		printk("sys_read: fd error\n");
		return -1;
	}

	ASSERT(buf != NULL);
	uint32_t fd_ = fd_local2global(fd);
	return file_read(&file_table[fd_], buf, count);
}

/* 重置用于文件读写操作的偏移指针，成功时返回新的偏移量，出错时返回-1 */
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
	if (fd < 0) {
		printk("sys_lseek: fd error\n");
		return -1;
	}

	ASSERT(whence > 0 && whence < 4);
	uint32_t fd_ = fd_local2global(fd);
	struct file* pf = &file_table[fd_];
	int32_t new_pos = 0;
	int32_t file_size = (int32_t)pf->fd_inode->i_size;

	switch (whence) {
		/* SEEK_SET：新的读写位置相对于文件开头增加offset */
		case SEEK_SET:
			new_pos = offset;
			break;
		/* SEEK_CUR：新的读写位置相对于文件当前位置增加offset */
		case SEEK_CUR:
			new_pos = (int32_t)pf->fd_pos + offset;    // offset可正可负
		/* SEEK_END：新的读写位置相对于文件尺寸增加offset */
		case SEEK_END:
			new_pos = file_size + offset;              // offset应为负值
	}

	if (new_pos < 0 || new_pos > (file_size - 1)) {
		return -1;
	}

	pf->fd_pos = new_pos;
	return pf->fd_pos;
}

/* 删除文件（非目录），成功返回0，失败返回-1 */
int32_t sys_unlink(const char* pathname) {
	ASSERT(strlen(pathname) < MAX_PATH_LEN);

	/* 先检查待删除的文件是否存在 */
	struct path_search_record searched_record;
	memset(&searched_record, 0, sizeof(struct path_search_record));
	int inode_no = search_file(pathname, &searched_record);
	ASSERT(inode_no != 0);
	if (inode_no == -1) {    // 未找到文件
		printk("file %s not found!\n", pathname);
		dir_close(searched_record.parent_dir);
		return -1;
	}

	if (searched_record.file_type == FT_DIRECTORY) {    // 目录
		printk("can not delete a directory with unlink(), use rmdir() to instead\n");
		dir_close(searched_record.parent_dir);
		return -1;
	}

	/* 检查是否在打开文件表中 */
	uint32_t file_idx = 0;
	while (file_idx < MAX_FILE_OPEN) {
		if (file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no) {
			break;
		}
		file_idx++;
	}
	if (file_idx < MAX_FILE_OPEN) {
		dir_close(searched_record.parent_dir);
		printk("file %s is in use, not allow to delete!\n", pathname);
		return -1;
	}
	ASSERT(file_idx == MAX_FILE_OPEN);

	/* 为delete_dir_entry申请缓冲区 */
	void* io_buf = sys_malloc(SECTOR_SIZE * 2);
	if (io_buf == NULL) {
		dir_close(searched_record.parent_dir);
		printk("sys_unlink: malloc for io_buf failed\n");
		return -1;
	}

	struct dir* parent_dir = searched_record.parent_dir;
	delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
	inode_release(cur_part, inode_no);
	sys_free(io_buf);
	dir_close(searched_record.parent_dir);
	/* 删除成功 */
	return 0;
}

/* 创建目录pathname，成功返回0，失败返回-1 */
int32_t sys_mkdir(const char* pathname) {
	uint8_t rollback_step = 0;    // 用于操作失败时回滚各资源状态
	void* io_buf = sys_malloc(SECTOR_SIZE * 2);
	if (io_buf == NULL) {
		printk("sys_mkdir: sys_malloc for io_buf failed\n");
		return -1;
	}

	/* 1. 确认待创建的新目录在文件系统上不存在 */
	struct path_search_record searched_record;
	memset(&searched_record, 0, sizeof(struct path_search_record));
	int inode_no = -1;
	inode_no = search_file(pathname, &searched_record);
	if (inode_no != -1) {    // 找到了同名目录或文件，失败返回
		printk("sys_mkdir: file or directory %s exist!\n", pathname);
		rollback_step = 1;
		goto rollback;
	} else {                 // 若未找到，需要判断是在最终目录没找到，还是某个中间目录不存在
		uint32_t pathname_depth = path_depth_cnt((char*)pathname);
		uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
		/* 没有访问到全部的路径，即某个中间目录不存在 */
		if (pathname_depth != path_searched_depth) {
			printk("sys_mkdir: can not access %s: Not a directory, subpath %s is not exist\n", pathname, searched_record.searched_path);
			rollback_step = 1;
			goto rollback;
		}
	}

	struct dir* parent_dir = searched_record.parent_dir;
	char* dirname = strrchr(searched_record.searched_path, '/') + 1;

	/* 2. 为新目录创建inode */
	inode_no = inode_bitmap_alloc(cur_part);
	if (inode_no == -1) {
		printk("sys_mkdir: allocate inode failed\n");
		rollback_step = 1;
		goto rollback;
	}

	struct inode new_dir_inode;
	inode_init(inode_no, &new_dir_inode);    // 初始化inode结点

	/* 3. 为新目录分配1个块存储该目录中的目录项.和.. */
	uint32_t block_bitmap_idx = 0;
	int32_t block_lba = -1;
	block_lba = block_bitmap_alloc(cur_part);
	if (block_lba == -1) {
		printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
		rollback_step = 2;
		goto rollback;
	}
	new_dir_inode.i_sectors[0] = block_lba;
	/* 每分配一个块就将块位图同步到硬盘 */
	block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
	ASSERT(block_bitmap_idx != 0);
	bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

	/* 4. 在新目录中创建目录项.和..，这是每个目录都必须存在的两个目录项 */
	memset(io_buf, 0, SECTOR_SIZE * 2);
	struct dir_entry* p_de = (struct dir_entry*)io_buf;

	/* 初始化当前目录. */
	memcpy(p_de->filename, ".", 1);
	p_de->i_no = inode_no;
	p_de->f_type = FT_DIRECTORY;

	/* 初始化当前目录父目录.. */
	p_de++;
	memcpy(p_de->filename, "..", 2);
	p_de->i_no = parent_dir->inode->i_no;
	p_de->f_type = FT_DIRECTORY;

	/* 将目录项同步到硬盘 */
	ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

	/* 更新目录大小 */
	new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

	/* 5. 在新目录的父目录中添加新目录的目录项 */
	struct dir_entry new_dir_entry;
	memset(&new_dir_entry, 0, sizeof(struct dir_entry));
	create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
	memset(io_buf, 0, SECTOR_SIZE * 2);
	if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
		printk("sys_mkdir: sync_dir_entry to disk failed!\n");
		rollback_step = 2;
		goto rollback;
	}

	/* 6. 将资源的变更同步到硬盘 */

	/* 父目录的inode同步到硬盘 */
	memset(io_buf, 0, SECTOR_SIZE * 2);
	inode_sync(cur_part, parent_dir->inode, io_buf);

	/* 新创建目录的inode同步到硬盘 */
	memset(io_buf, 0, SECTOR_SIZE * 2);
	inode_sync(cur_part, &new_dir_inode, io_buf);

	/* 将inode位图同步到硬盘 */
	bitmap_sync(cur_part, inode_no, INODE_BITMAP);

	/* 7. 资源回收 */
	sys_free(io_buf);

	/* 关闭新创建目录的父目录 */
	dir_close(searched_record.parent_dir);
	return 0;

/* 创建目录需要创建相关的多个资源，若某步失败则执行下面的回滚步骤 */
rollback:
	switch (rollback_step) {
		case 2:
			/* 如果新目录的inode创建失败，之前位图中分配的inode_no也要恢复 */
			bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
		case 1:
			/* 关闭新创建目录的父目录 */
			dir_close(searched_record.parent_dir);
			break;
	}
	sys_free(io_buf);
	return -1;
}

/* 目录成功打开返回目录指针，失败返回NULL */
struct dir* sys_opendir(const char* name) {
	ASSERT(strlen(name) < MAX_PATH_LEN);

	/* 如果是根目录，直接返回&root_dir */
	if (name[0] == '/' && (name[1] == 0 || name[1] == '.')) {
		return &root_dir;
	}

	/* 检查待打开的目录是否存在 */
	struct path_search_record searched_record;
	memset(&searched_record, 0, sizeof(struct path_search_record));
	int inode_no = search_file(name, &searched_record);
	struct dir* ret = NULL;
	if (inode_no == -1) {
		printk("In %s, sub path %s not exist\n", name, searched_record.searched_path);
	} else {
		if (searched_record.file_type == FT_REGULAR) {
			printk("%s is regular file!\n", name);
		} else if (searched_record.file_type == FT_DIRECTORY) {
			ret = dir_open(cur_part, inode_no);
		}
	}
	dir_close(searched_record.parent_dir);
	return ret;
}

/* 目录成功关闭返回0，失败返回-1 */
int32_t sys_closedir(struct dir* dir) {
	int32_t ret = -1;
	if (dir != NULL) {
		dir_close(dir);
		ret = 0;
	}
	return ret;
}

/* 读取目录dir的一个目录项，成功返回目录项地址，到目录尾或出错返回NULL */
struct dir_entry* sys_readdir(struct dir* dir) {
	ASSERT(dir != NULL);
	return dir_read(dir);
}

/* 删除空目录，成功则返回0，失败返回-1 */
int32_t sys_rmdir(const char* pathname) {
	/* 先检查待删除的目录是否存在 */
	struct path_search_record searched_record;
	memset(&searched_record, 0, sizeof(struct path_search_record));
	int inode_no = search_file(pathname, &searched_record);
	ASSERT(inode_no != 0);

	int retval = -1;    // 默认返回-1
	if (inode_no == -1) {
		printk("In %s, sub path %s not exist\n", pathname, searched_record.searched_path);
	} else {
		if (searched_record.file_type == FT_REGULAR) {
			printk("%s is regular file!\n", pathname);
		} else {
			struct dir* dir = dir_open(cur_part, inode_no);
			if (!dir_is_empty(dir)) {    // 非空目录不能删除
				printk("dir %s is not empty, it is not allowed to delete a nonempty directory!\n", pathname);
			} else {
				if (!dir_remove(searched_record.parent_dir, dir)) {
					retval = 0;
				}
			}
			dir_close(dir);
		}
	}
	dir_close(searched_record.parent_dir);
	return retval;
}

/* 将目录dir的指针dir_pos置0 */
void sys_rewinddir(struct dir* dir) {
	dir->dir_pos = 0;
}

/* 在磁盘上搜索文件系统，若没有文件系统则格式化分区创建文件系统 */
void filesys_init() {
	uint8_t channel_no = 0, dev_no, part_idx = 0;

	/* sb_buf用来存储从硬盘分区上读入的超级块 */
	struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);
	if (sb_buf == NULL) {
		PANIC("alloc memory failed!");
	}

	printk("searching filesystem......\n");
	while (channel_no < channel_cnt) {
		dev_no = 0;
		while (dev_no < 2) {
			/* 跨过裸盘hd60M.img */
			if (dev_no == 0) {
				dev_no++;
				continue;
			}
			struct disk* hd = &channels[channel_no].devices[dev_no];
			struct partition* part = hd->prim_parts;
			/* 4个主分区 + 8个逻辑分区 */
			while (part_idx < 12) {
				if (part_idx == 4) {    // 开始处理逻辑分区
					part = hd->logic_parts;
				}

				/* channels数组是全局变量，默认值为0，disk属于其嵌套结构，partition又是disk的嵌套结构，*/
				/* 因此partition中的成员默认为0，若partition未初始化，则partition中的成员仍然为0 */
				if (part->sec_cnt != 0) {    // 如果分区存在
					memset(sb_buf, 0, SECTOR_SIZE);

					/* 读出分区的超级块，根据魔数判断是否存在文件系统 */
					ide_read(hd, part->start_lba + 1, sb_buf, 1);

					/* 只支持自己的文件系统，若已经存在文件系统则不再格式化 */
					if (sb_buf->magic == 0x19590318) {
						printk("%s has filesystem\n", part->name);
					} else {    // 其他文件系统不支持，一律按无文件系统处理
						printk("formatting %s partition %s......\n", hd->name, part->name);
						partition_format(part);
					}
				}
				part_idx++;
				part++;    // 处理下一分区
			}
			dev_no++;    // 处理下一磁盘
		}
		channel_no++;    // 处理下一通道
	}
	sys_free(sb_buf);

	/* 确定默认操作的分区 */
	char default_part[8] = "sdb1";
	/* 挂载分区 */
	list_traversal(&partition_list, mount_partition, (int)default_part);

	/* 将当前分区的根目录打开 */
	open_root_dir(cur_part);

	/* 初始化文件表 */
	uint32_t fd_idx = 0;
	while (fd_idx < MAX_FILE_OPEN) {
		file_table[fd_idx++].fd_inode = NULL;
	}
}
