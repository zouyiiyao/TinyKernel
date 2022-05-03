#include "shell.h"
#include "stdint.h"
#include "fs.h"
#include "file.h"
#include "syscall.h"
#include "stdio.h"
#include "global.h"
#include "assert.h"    // lib/user/assert.h
#include "string.h"
#include "buildin_cmd.h"

/* 最大支持键入128个字符的命令行输入 */
#define cmd_len 128

/* 除命令本身，最多支持15个参数 */
#define MAX_ARG_NR 16

/* 存储输入的命令 */
static char cmd_line[cmd_len] = {0};

/* 用于解析路径时的缓冲区 */
char final_path[MAX_PATH_LEN] = {0};

/* 用来记录当前目录，是当前目录的缓存，每次执行cd命令时会更新此内容 */
char cwd_cache[64] = {0};

/* 输出提示符 */
void print_prompt(void) {
	printf("[rabbit@localhost %s]$ ", cwd_cache);
}

/* 从键盘缓冲区中最多读入count个字节到buf */
static void readline(char* buf, int32_t count) {
	assert(buf != NULL && count > 0);

	char* pos = buf;
	/* 在不出错的情况下，直到找到回车符才返回 */
	while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count) {
		switch (*pos) {
			/* 找到回车符或换行符后认为键入的命令结束，直接返回 */
			case '\n':
			case '\r':
				*pos = 0;    // 添加cmd_line的终止字符0
				putchar('\n');
				return;

			case '\b':
				if (buf[0] != '\b') {    // 阻止删除非本次输入的命令信息
					--pos;
					putchar('\b');
				}
				break;

			/* Ctrl+l清屏 */
			case 'l' - 'a':
				/* 1. 先将当前字符'l' - 'a'置为0 */
				*pos = 0;
				/* 2. 再将屏幕清空 */
				clear();
				/* 3. 打印提示符 */
				print_prompt();
				/* 4. 将之前键入的内容再次打印 */
				printf("%s", buf);
				break;

			/* Ctrl+u清除输入 */
			case 'u' - 'a':
				while (buf != pos) {
					putchar('\b');
					*(pos--) = 0;
				}
				break;

			default:
				putchar(*pos);
				pos++;
		}
	}

	printf("readline: can not find enter_key in the cmd_line, max num of char is 128\n");
}

/* 解析字符串cmd_str中以token为分隔符的单词，将各单词的指针存入argv数组 */
static int32_t cmd_parse(char* cmd_str, char** argv, char token) {
	assert(cmd_str != NULL);

	int32_t arg_idx = 0;
	while (arg_idx < MAX_ARG_NR) {
		argv[arg_idx] = NULL;
		arg_idx++;
	}

	char* next = cmd_str;
	int32_t argc = 0;
	/* 外层循环处理整个命令行 */
	while (*next) {
		/* 去除命令或参数之间的空格 */
		while (*next == token) {
			next++;
		}

		/* 处理最后一个参数后接空格的情况，如"ls dir2 " */
		if (*next == 0) {
			break;
		}

		argv[argc] = next;

		/* 内层循环处理命令行中的每个命令或参数 */
		while (*next && *next != token) {
			next++;
		}

		/* 如果遇到token字符，将其置为0 */
		if (*next) {
			*next++ = 0;    // 将token字符替换为字符串结束符0，并将指针next指向下一个字符
		}

		/* 避免argv数组访问越界 */
		if (argc > MAX_ARG_NR) {
			return -1;
		}

		argc++;
	}
	return argc;
}

/* argv必须为全局变量，使得以后exec的程序可以访问参数 */
char* argv[MAX_ARG_NR];

int32_t argc = -1;

/* 简单的shell */
void my_shell(void) {
	cwd_cache[0] = '/';
	while (1) {
		print_prompt();
		memset(cmd_line, 0, cmd_len);
		memset(final_path, 0, MAX_PATH_LEN);

		readline(cmd_line, cmd_len);
		if (cmd_line[0] == 0) {    // 若只键入了一个回车
			continue;
		}

		argc = cmd_parse(cmd_line, argv, ' ');
		if (argc == -1) {
			printf("num of arguments exceed %d\n", MAX_ARG_NR);
			continue;
		}

		char buf[MAX_PATH_LEN] = {0};
		int32_t arg_idx = 0;
		while (arg_idx < argc) {
			make_clear_abs_path(argv[arg_idx], buf);
			printf("%s -> %s\n", argv[arg_idx], buf);
			arg_idx++;
		}
		printf("\n");
	}
	panic("my_shell: should not be here");
}
