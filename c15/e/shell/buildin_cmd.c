#include "buildin_cmd.h"
#include "syscall.h"
#include "stdio.h"
#include "string.h"
#include "fs.h"
#include "global.h"
#include "dir.h"
#include "shell.h"
#include "assert.h"

/* 将路径old_abs_path中的.和..转化为实际路径后存入new_abs_path */
static void wash_path(char* old_abs_path, char* new_abs_path) {
	assert(old_abs_path[0] == '/');
	char name[MAX_FILE_NAME_LEN] = {0};
	char* sub_path = old_abs_path;
	sub_path = path_parse(sub_path, name);
	if (name[0] == 0) {
		new_abs_path[0] = '/';
		new_abs_path[1] = 0;
		return;
	}

	new_abs_path[0] = 0;
	strcat(new_abs_path, "/");
	while (name[0]) {
		/* 如果是上一级目录.. */
		if (!strcmp("..", name)) {
			char* slash_ptr = strrchr(new_abs_path, '/');
			if (slash_ptr != new_abs_path) {    // 如"/a/b/.." => "/a"
				*slash_ptr = 0;
			} else {                            // 如"/a/.." => "/"
				*(slash_ptr + 1) = 0;
			}
		/* 如果是正常目录 */
		} else if (strcmp(".", name)) {
			if (strcmp(new_abs_path, "/")) {
				strcat(new_abs_path, "/");
			}
			strcat(new_abs_path, name);
		}
		/* 如果是当前目录.，无需处理new_abs_path */

		/* 继续解析下一层路径 */
		memset(name, 0, MAX_FILE_NAME_LEN);
		if (sub_path) {
			sub_path = path_parse(sub_path, name);
		}
	}
}

/* 将path处理成不含.和..的绝对路径，存储在final_path中 */
void make_clear_abs_path(char* path, char* final_path) {
	char abs_path[MAX_PATH_LEN] = {0};

	/* 先判断输入是否为绝对路径，若不是绝对路径，先转为绝对路径 */
	if (path[0] != '/') {
		memset(abs_path, 0, MAX_PATH_LEN);
		if (getcwd(abs_path, MAX_PATH_LEN) != NULL) {
			if (!((abs_path[0] == '/') && (abs_path[1] == 0))) {    // abs_path不为"/"
				strcat(abs_path, "/");
			}
		}
	}
	strcat(abs_path, path);

	wash_path(abs_path, final_path);
}
