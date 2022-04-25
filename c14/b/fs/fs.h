#ifndef __FS_FS_H
#define __FS_FS_H

#include "stdint.h"
#include "ide.h"

/* 每个分区所支持最大创建的文件数 */
#define MAX_FILES_PER_PART 4096

/* 每个扇区位数 */
#define BITS_PER_SECTOR 4096

/* 每个扇区字节数 */
#define SECTOR_SIZE 512

/* 每个块字节数 */
#define BLOCK_SIZE SECTOR_SIZE

/* 文件类型 */
enum file_types {
	FT_UNKNOWN,     // 不支持的文件类型
	FT_REGULAR,     // 普通文件
	FT_DIRECTORY    // 目录
};

void filesys_init(void);

#endif
