#include "print.h"
#include "init.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall_init.h"
#include "syscall.h"
#include "stdio.h"
#include "fs.h"
#include "stdio_kernel.h"
#include "dir.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
int prog_a_pid = 0, prog_b_pid = 0;

int main(void) {
	put_str("I am kernel\n");
	init_all();

	// process_execute(u_prog_a, "u_prog_a");	
	// process_execute(u_prog_b, "u_prog_b");	
	// thread_start("k_thread_a", 31, k_thread_a, "I am thread_a");
	// thread_start("k_thread_b", 31, k_thread_b, "I am thread_b");

	struct dir* p_dir = sys_opendir("/dir1");
	if (p_dir) {
		printf("/dir1 content before delete /dir1/subdir1:\n");
		char* type = NULL;
		struct dir_entry* dir_e = NULL;
		while ((dir_e = sys_readdir(p_dir))) {
			if (dir_e->f_type == FT_REGULAR) {
				type = "regular";
			} else {
				type = "directory";
			}
			printf("    %s    %s\n", type, dir_e->filename);
		}

		printf("try to delete nonempty directory /dir1/subdir1\n");
		if (sys_rmdir("/dir1/subdir1") == -1) {
			printf("sys_rmdir: /dir1/subdir1 delete fail!\n");
		}

		printf("try to delete /dir1/subdir1/file2\n");
		if (sys_rmdir("/dir1/subdir1/file2") == -1) {
			printf("sys_rmdir: /dir1/subdir1/file2 delete fail!\n");
		}
		if (sys_unlink("/dir1/subdir1/file2") == 0) {
			printf("sys_unlink: /dir1/subdir1/file2 delete done!\n");
		}

		printf("try to delete directory /dir1/subdir1 again\n");
		if (sys_rmdir("/dir1/subdir1") == 0) {
			printf("sys_rmdir: /dir1/subdir1 delete done!\n");
		}

		printf("/dir1 content after delete /dir1/subdir1:\n");
		sys_rewinddir(p_dir);
		while ((dir_e = sys_readdir(p_dir))) {
			if (dir_e->f_type == FT_REGULAR) {
				type = "regular";
			} else {
				type = "directory";
			}
			printf("    %s    %s\n", type, dir_e->filename);
		}

		if (sys_closedir(p_dir) == 0) {
			printf("/dir1 close done!\n");
		} else {
			printf("/dir1 close fail!\n");
		}
	} else {
		printf("/dir1 open fail!\n");
	}

	while(1);
	return 0;
}

/* ??????????????????????????? */
void k_thread_a(void* arg) {
	char* para = arg;
	void* addr1 = sys_malloc(256);
	void* addr2 = sys_malloc(255);
	void* addr3 = sys_malloc(254);
	// console_put_str("thread_a malloc addr:0x");
	// console_put_int((int)addr1);
	// console_put_char(',');
	// console_put_int((int)addr2);
	// console_put_char(',');
	// console_put_int((int)addr3);
	// console_put_char('\n');
	
	printk("thread_a malloc addr:0x%x, 0x%x, 0x%x\n", (int)addr1, (int)addr2, (int)addr3);

	int cpu_delay = 100000;
	while (cpu_delay-- > 0);
	sys_free(addr1);
	sys_free(addr2);
	sys_free(addr3);
	
	while (1);
}

/* ??????????????????????????? */
void k_thread_b(void* arg) {
	char* para = arg;
	void* addr1 = sys_malloc(256);
	void* addr2 = sys_malloc(255);
	void* addr3 = sys_malloc(254);
	// console_put_str("thread_b malloc addr:0x");
	// console_put_int((int)addr1);
	// console_put_char(',');
	// console_put_int((int)addr2);
	// console_put_char(',');
	// console_put_int((int)addr3);
	// console_put_char('\n');

	printk("thread_b malloc addr:0x%x, 0x%x, 0x%x\n", (int)addr1, (int)addr2, (int)addr3);

	int cpu_delay = 100000;
	while (cpu_delay-- > 0);
	sys_free(addr1);
	sys_free(addr2);
	sys_free(addr3);
	
	while (1);
}

/* ?????????????????? */
void u_prog_a(void) {
	void* addr1 = malloc(256);
	void* addr2 = malloc(255);
	void* addr3 = malloc(254);
	printf("prog_a malloc addr:0x%x,0x%x,0x%x\n", (int)addr1, (int)addr2, (int)addr3);

	int cpu_delay = 100000;
	while (cpu_delay-- > 0);
	free(addr1);
	free(addr2);
	free(addr3);
	while (1);
}

/* ?????????????????? */
void u_prog_b(void) {
	void* addr1 = malloc(256);
	void* addr2 = malloc(255);
	void* addr3 = malloc(254);
	printf("prog_b malloc addr:0x%x,0x%x,0x%x\n", (int)addr1, (int)addr2, (int)addr3);

	int cpu_delay = 100000;
	while (cpu_delay-- > 0);
	free(addr1);
	free(addr2);
	free(addr3);
	while (1);
}
