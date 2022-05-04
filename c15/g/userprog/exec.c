#include "exec.h"
#include "thread.h"
#include "stdio_kernel.h"
#include "fs.h"
#include "string.h"
#include "global.h"
#include "memory.h"

extern void intr_exit(void);

typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* 32位elf文件头 */
struct Elf32_Ehdr {
	unsigned char e_ident[16];
	Elf32_Half e_type;
	Elf32_Half e_machine;
	Elf32_Word e_version;
	Elf32_Addr e_entry;
	Elf32_Off e_phoff;
	Elf32_Off e_shoff;
	Elf32_Word e_flags;
	Elf32_Half e_ehsize;
	Elf32_Half e_phentsize;
	Elf32_Half e_phnum;
	Elf32_Half e_shentsize;
	Elf32_Half e_shnum;
	Elf32_Half e_shstrndx;
};

/* 程序段头Program header */
struct Elf32_Phdr {
	Elf32_Word p_type;      // 枚举值见segment_type
	Elf32_Off p_offset;     // 本段在文件内的偏移量
	Elf32_Addr p_vaddr;     // 本段被加载到内存后的起始虚拟地址
	Elf32_Addr p_paddr;     // 保留项
	Elf32_Word p_filesz;    // 本段在文件中的字节大小
	Elf32_Word p_memsz;     // 本段在内存中的字节大小，等于p_filesz
	Elf32_Word p_flags;     // 读、写、执行等段标志
	Elf32_Word p_align;     // 本段对齐方式
};

/* 段类型 */
enum segment_type {
	PT_NULL,                // 忽略
	PT_LOAD,                // 可加载程序段，包括数据段和代码段，只关注这一种段类型即可
	PT_DYNAMIC,             // 动态加载
	PT_INTERP,              // 动态加载器名称
	PT_NOTE,                // 辅助信息
	PT_SHLIB,               // 保留
	PT_PHDR                 // 程序头表
};

/* 将文件描述符fd指向的文件中偏移为offset、大小为filesz的段加载到虚拟地址为vaddr的内存中 */
static bool segment_load(int32_t fd, uint32_t offset, uint32_t filesz, uint32_t vaddr) {
	uint32_t vaddr_first_page = vaddr & 0xfffff000;                  // vaddr所在的页
	uint32_t size_in_first_page = PG_SIZE - (vaddr & 0x00000fff);    // 加载到内存后，文件在第一个页中占用的字节数
	uint32_t occupy_pages = 0;

	/* 如果一个页容不下该段 */
	if (filesz > size_in_first_page) {
		uint32_t left_size = filesz - size_in_first_page;
		occupy_pages = DIV_ROUND_UP(left_size, PG_SIZE) + 1;
	} else {
		occupy_pages = 1;
	}

	/* 为进程分配内存 */
	uint32_t page_idx = 0;
	uint32_t vaddr_page = vaddr_first_page;
	while (page_idx < occupy_pages) {
		uint32_t* pde = pde_ptr(vaddr_page);
		uint32_t* pte = pte_ptr(vaddr_page);

		/* 如果pde不存在，或者pte不存在，说明页未分配 */
		if (!(*pde & 0x00000001) || !(*pte & 0x00000001)) {
			if (get_a_page(PF_USER, vaddr_page) == NULL) {
				return false;
			}
		}
		/* 如果被覆盖的进程中页已经分配，则直接覆盖 */

		vaddr_page += PG_SIZE;
		page_idx++;
	}

	sys_lseek(fd, offset, SEEK_SET);
	sys_read(fd, (void*)vaddr, filesz);

	return true;
}

/* 从文件系统上加载用户程序pathname，成功则返回程序入口地址，失败返回-1 */
static int32_t load(const char* pathname) {
	int32_t ret = -1;
	struct Elf32_Ehdr elf_header;
	struct Elf32_Phdr prog_header;
	memset(&elf_header, 0, sizeof(struct Elf32_Ehdr));

	int32_t fd = sys_open(pathname, O_RDONLY);
	if (fd == -1) {
		return -1;
	}

	if (sys_read(fd, &elf_header, sizeof(struct Elf32_Ehdr)) != sizeof(struct Elf32_Ehdr)) {
		ret = -1;
		goto done;
	}

	/* 校验elf头 */
	if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7) \
	    || elf_header.e_type != 2 \
	    || elf_header.e_machine != 3 \
	    || elf_header.e_version != 1 \
	    || elf_header.e_phnum > 1024 \
	    || elf_header.e_phentsize != sizeof(struct Elf32_Phdr)) {
		ret = -1;
		goto done;
	}

	/* 遍历所有程序段头 */
	Elf32_Off prog_header_offset = elf_header.e_phoff;
	Elf32_Half prog_header_size = elf_header.e_phentsize;

	uint32_t prog_idx = 0;
	while (prog_idx < elf_header.e_phnum) {
		memset(&prog_header, 0, prog_header_size);

		/* 将文件指针移动到程序段头表起始位置 */
		sys_lseek(fd, prog_header_offset, SEEK_SET);

		/* 读取一个程序段头 */
		if (sys_read(fd, &prog_header, prog_header_size) != prog_header_size) {
			ret = -1;
			goto done;
		}

		/* 如果是可加载段就调用segment_load加载到内存 */
		if (PT_LOAD == prog_header.p_type) {
			if (!segment_load(fd, prog_header.p_offset, prog_header.p_filesz, prog_header.p_vaddr)) {
				ret = -1;
				goto done;
			}
		}

		/* 更新程序段头偏移，指向下一个程序段头 */
		prog_header_offset += elf_header.e_phentsize;

		prog_idx++;
	}
	ret = elf_header.e_entry;

done:
	sys_close(fd);
	return ret;
}

/* 用path指向的程序替换当前进程 */
int32_t sys_execv(const char* path, const char* argv[]) {
	uint32_t argc = 0;
	while (argv[argc]) {
		argc++;
	}

	int32_t entry_point = load(path);
	if (entry_point == -1) {
		return -1;
	}

	struct task_struct* cur = running_thread();
	/* 修改进程名 */
	memcpy(cur->name, path, TASK_NAME_LEN);
	cur->name[TASK_NAME_LEN - 1] = 0;

	/* 构造从中断返回时的内核栈，内核栈中的内容表示中断返回后各个寄存器中的内容以及返回到哪条指令执行 */
	struct intr_stack* intr_0_stack = (struct intr_stack*)((uint32_t)cur + PG_SIZE - sizeof(struct intr_stack));
	/* 中断返回后从寄存器ebx和ecx中读取进一步传递给新进程main函数的参数，入用户栈 */
	intr_0_stack->ebx = (int32_t)argv;
	intr_0_stack->ecx = argc;
	intr_0_stack->eip = (void*)entry_point;
	/* 使新进程用户栈地址为最高用户空间地址 */
	intr_0_stack->esp = (void*)0xc0000000;

	/* exec不同于fork，为使新进程尽快被执行，直接从中断中返回 */
	asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (intr_0_stack) : "memory");

	return 0;
}
