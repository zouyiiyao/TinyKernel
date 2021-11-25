#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "debug.h"
#include "string.h"

/* 物理页大小 */
#define PG_SIZE 4096

/* 位图地址 */
#define MEM_BITMAP_BASE 0xc009a000

/* 定义从虚拟地址取PDE和PTE索引的宏 */
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

/* 内核堆的起始虚拟地址：0xc0000000是内核从虚拟地址3G开始，加0x100000指跨过已经映射的低端1MB内存，使虚拟地址在逻辑上连续 */
#define K_HEAP_START 0xc0100000

/* 物理内存池结构，生成两个实例用于管理内核物理内存池和用户物理内存池 */
struct pool {
	struct bitmap pool_bitmap;     // 本内存池用到的位图结构，用于管理物理内存
	uint32_t phy_addr_start;       // 本内存池所管理物理内存的起始地址
	uint32_t pool_size;            // 本内存池所管理物理内存的字节容量
};

struct pool kernel_pool, user_pool;    // 生成内核物理内存池和用户物理内存池
struct virtual_addr kernel_vaddr;      // 此结构用来给内核分配虚拟地址

/* 在pf表示的虚拟内存池中申请pg_cnt个虚拟页，成功则返回虚拟页的起始地址，失败则返回NULL */
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
	int vaddr_start = 0, bit_idx_start = -1;
	uint32_t cnt = 0;
	if (pf == PF_KERNEL) {
		bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
		if (-1 == bit_idx_start) {
			return NULL;
		}
		while (cnt < pg_cnt) {
			bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
		}
		vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
	// 用户内存池，将来实现用户进程时再补充
	} else {
	}
	return (void*)vaddr_start;
}

/* 返回虚拟地址vaddr对应的pte指针，取内容为页物理地址 */
uint32_t* pte_ptr(uint32_t vaddr) {
	// 0xc0000000 => 0xfff00000
	uint32_t* pte = (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
	return pte;
}

/* 返回虚拟地址vaddr对应的pde指针，取内容为页表物理地址 */
uint32_t* pde_ptr(uint32_t vaddr) {
	// 0xc0000000 => 0xfffff000
	uint32_t* pde = (uint32_t*)((0xfffff000) + PDE_IDX(vaddr) * 4);
	return pde;
}

/* 在m_pool指向的物理内存池中分配1个物理页，成功则返回页的物理地址，失败返回NULL */
static void* palloc(struct pool* m_pool) {
	// 扫描或设置位图要保证原子操作
	int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);    // 找到一个空闲的物理页面
	if (-1 == bit_idx) {
		return NULL;
	}
	bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);          // 将此位置1，表示已经分配出去了
	uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
	return (void*)page_phyaddr;
}

/* 页表中添加虚拟地址_vaddr（页）与物理地址_page_phyaddr（页）的映射 */
static void page_table_add(void* _vaddr, void* _page_phyaddr) {
	uint32_t vaddr = (uint32_t)_vaddr, page_phyaddr = (uint32_t)_page_phyaddr;
	uint32_t* pde = pde_ptr(vaddr);    // 存储页表物理地址的虚拟地址（指针）
	uint32_t* pte = pte_ptr(vaddr);    // 存储页物理地址的虚拟地址（指针）
		
	// 判断页目录项的P位，若为1，说明页表已经存在
	if (*pde & 0x00000001) {
		// 必须是目前还没有建立映射，即页表项的P位为0
		ASSERT(!(*pte & 0x00000001));

		if (!(*pte & 0x00000001)) {
			*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);    // 用户权限，可读/写/执行，页表项存在
		// 不会执行到这里
		} else {
			PANIC("pte repeat");
		}
	// 页目录项不存在，需要先创建页表
	} else {
		// 分配页表需要使用的物理页，从内核内存池中分配
		uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);
		*pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);             // 用户权限，可读/写/执行，页目录项存在
		
		// 分配到的物理页地址pde_phyaddr对应的内容清0，避免把里面的陈旧数据当成页表项，取高20位获取到刚申请的物理页的起始虚拟地址
		memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE);

		ASSERT(!(*pte & 0x00000001));
		*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);            // 用户权限，可读/写/执行，页目录项存在
	} 
}

/* 根据pf提供的类型，在指定内存池中分配pg_cnt个页空间，成功则返回起始虚拟地址，失败时返回NULL */
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
	ASSERT(pg_cnt > 0 && pg_cnt < 3840);
	
	// malloc_page总共需要分三步：
	
	// 1. 通过vaddr_get在虚拟内存池中申请虚拟页
	void* vaddr_start = vaddr_get(pf, pg_cnt);
	if (vaddr_start == NULL) {
		return NULL;
	}

	uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
	struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

	// 由于虚拟页是连续的，而物理页可以是不连续的，所以逐一建立映射
	while (cnt-- > 0) {
		// 2. 通过palloc在物理内存池中申请一个物理页
		void* page_phyaddr = palloc(mem_pool);
		// 分配失败时要把已经申请的虚拟页和物理页全部回滚，等未来实现内存回收时再补充
		if (page_phyaddr == NULL) {
			return NULL;
		}
		// 3. 通过page_table_add将虚拟页和物理页映射建立
		page_table_add((void*)vaddr, page_phyaddr);
		vaddr += PG_SIZE;    // 处理下一虚拟页
	}
	return vaddr_start;
}

/* 从内核物理内存池中申请页，成功返回其虚拟地址，失败返回NULL，调用malloc_page完成实际的工作 */
void* get_kernel_pages(uint32_t pg_cnt) {
	void* vaddr = malloc_page(PF_KERNEL, pg_cnt);
	if (vaddr != NULL) {
		memset(vaddr, 0, pg_cnt * PG_SIZE);    // 将分配的虚拟页内容都清0
	}
	return vaddr;
}

/* 初始化内存池 */
static void mem_pool_init(uint32_t all_mem) {
	put_str("    mem_pool_init start\n");
	// 页表大小：页目录表占1页 + 第0个和第768个页目录项指向同一个页表 + 第769-1022个页目录项共指向254个页表，共256个页表
	uint32_t page_table_size = PG_SIZE * 256;
	// 低端物理内存1MB，加上页表使用物理内存1MB
	uint32_t used_mem = page_table_size + 0x100000;
	// 一共32MB物理内存，32MB - 2MB = 30MB
	uint32_t free_mem = all_mem - used_mem;
	// 以页（4KB）为单位的内存分配策略，共7680页
	uint16_t all_free_pages = free_mem / PG_SIZE;
	// 物理内存由内核和用户进程平分，各3840页
	uint16_t kernel_free_pages = all_free_pages / 2;
	uint16_t user_free_pages = all_free_pages - kernel_free_pages;
	// kernel bitmap的字节数，位图中每一位表示一页
	uint32_t kbm_length = kernel_free_pages / 8;
	// user bitmap的字节数
	uint32_t ubm_length = user_free_pages / 8;
	// kernel pool start，内核内存池的起始地址
	uint32_t kp_start = used_mem;
	// user pool start，用户内存池的起始地址
	uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;

	// kernel_pool内存池：0x200000 - 0x10FFFFF
	kernel_pool.phy_addr_start = kp_start;
	// user_pool内存池：0x1100000 - 0x1FFFFFF
	user_pool.phy_addr_start = up_start;

	kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
	user_pool.pool_size = user_free_pages * PG_SIZE;

	kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
	user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

	// 内核内存池的位图起始地址固定在MEM_BITMAP_BASE（0xc009a000）处，32MB内存占用位图2KB
	kernel_pool.pool_bitmap.bits = (void*)MEM_BITMAP_BASE;
	// 用户内存池的位图紧跟在内核内存池之后（0xc009a1e0），0x1e0 * 8 * 4K = 0xF00000
	user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);

	// 输出内存池信息
	put_str("        kernel_pool_bitmap_start:");
	put_int((int)kernel_pool.pool_bitmap.bits);
	put_str(" kernel_pool_phy_addr_start:");
	put_int(kernel_pool.phy_addr_start);
	put_str("\n");
	put_str("        user_pool_bitmap_start:");
	put_int((int)user_pool.pool_bitmap.bits);
	put_str(" user_pool_phy_addr_start:");
	put_int(user_pool.phy_addr_start);
	put_str("\n");

	// 将位图置0
	bitmap_init(&kernel_pool.pool_bitmap);
	bitmap_init(&user_pool.pool_bitmap);

	// 初始化内核虚拟地址的位图，按实际物理内存大小生成数组
	kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
	// 内核虚拟地址内存池的位图跟在用户物理内存池的位图之后
	kernel_vaddr.vaddr_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length);
	kernel_vaddr.vaddr_start = K_HEAP_START;
	bitmap_init(&kernel_vaddr.vaddr_bitmap);
	put_str("    mem_pool_init done\n"); 
}

/* 内存管理部分初始化 */
void mem_init() {
	put_str("mem_init start\n");
	uint32_t mem_bytes_total = (*(uint32_t*)(0xb00));
	mem_pool_init(mem_bytes_total);    // 初始化内存池
	put_str("mem_init done\n");
}
