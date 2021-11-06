; 内核加载程序
; -----------------------------------------------------------

%include "boot.inc"

section loader vstart=LOADER_BASE_ADDR

LOADER_STACK_TOP equ LOADER_BASE_ADDR

	; 构建gdt及其内部的描述符，加载到内存中
	GDT_BASE:
	dd 0x00000000
	dd 0x00000000

	CODE_DESC:
	dd 0x0000FFFF
	dd DESC_CODE_HIGH4

	DATA_STACK_DESC:
	dd 0x0000FFFF
	dd DESC_DATA_HIGH4

	VIDEO_DESC:
	dd 0x80000007          ; limit = (0xbffff - 0xb8000) / 4k = 0x7
	dd DESC_VIDEO_HIGH4    ; 此时DPL为0

	GDT_SIZE equ $ - GDT_BASE
	GDT_LIMIT equ GDT_SIZE - 1
	
	times 60 dq 0          ; 此处预留60个描述符的空位

	; total_mem_bytes：用于保存物理内存容量，以字节为单位，此位置比较好记
	; 当前偏移loader.bin文件头0x200字节（64 * 8），loader.bin被MBR加载到物理内存0x900，
	; 故total_mem_bytes在物理内存中的地址是0xb00，将来在内核中我们会引用该地址
	total_mem_bytes dd 0

	; 段选择子
	SELECTOR_CODE  equ (0x0001<<3) + TI_GDT + RPL0    ; 相当于(CODE_DESC - GDT_BASE) / 8 + TI_GDT + RPL0
	SELECTOR_DATA  equ (0x0002<<3) + TI_GDT + RPL0    ; 相当于(DATA_DESC - GDT_BASE) / 8 + TI_GDT + RPL0
	SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0    ; 相当于(VIDEO_DESC - GDT_BASE) / 8 + TI_GDT + RPL0

	; 以下是gdt的指针，前2字节是gdt界限，后4字节是gdt起始地址
	gdt_ptr dw GDT_LIMIT
	        dd GDT_BASE

	; 人工对齐：total_mem_bytes(4) + gdt_ptr(6) + ards_buf(244) + ards_nr(2)，共256字节
	ards_buf times 244 db 0
	ards_nr dw 0                                      ; 用于记录ARDS结构体数量
	
	loader_start:                                     ; loader入口地址，0xc00，也是loader被执行的第一条指令

	; 测试e801号与88号子功能
	; jmp .e820_failed_so_try_e801
	; jmp .e801_failed_so_try_88
 
	; 获取物理内存布局
	; int 15h, eax = 0000E820h, edx = 534D4150h('SMAP')
	xor ebx, ebx                            ; 第一次调用时，ebx值要为0
	mov edx, 0x534d4150                     ; edx只赋值一次，循环体中不会改变
	mov di, ards_buf                        ; 将ards结构缓冲区指针赋值给di
	.e820_mem_get_loop:                     ; 循环获取每个ARDS内存范围描述结构
	mov eax, 0x0000e820                     ; 执行int 0x15后，eax的值会变为0x534d4150，所以每次执行int 0x15之前都要重新赋值为子功能号
	mov ecx, 20                             ; ARDS地址范围描述符结构大小为是20字节
	int 0x15
	jc .e820_failed_so_try_e801             ; 若cf位为1，说明有错误发生，则尝试0xe801号子功能
	add di, cx                              ; 使di增加20字节指向缓冲区中新的ARDS结构位置
	inc word [ards_nr]                      ; 记录ARDS数量
	cmp ebx, 0                              ; 若ebx为0且cf不为1，这说明ards全部返回，当前已经是最后一个
	jnz .e820_mem_get_loop

	; 在所有ards结构中，找出(base_add_low + length_low)的最大值，即内存的容量
	mov cx, [ards_nr]
	; 遍历每一个ARDS结构体，循环次数是ARDS的数量
	mov ebx, ards_buf
	xor edx, edx                            ; edx存最大的内存容量，在此先清0
	; 无需判断Type是否为1，最大的内存块一定是可被使用的
	.find_max_mem_area:
	mov eax, [ebx]                          ; base_add_low
	add eax, [ebx + 8]                      ; length_low
	add ebx, 20                             ; 指向缓冲区中下一个ARDS结构
	cmp edx, eax
	jge .next_ards
	mov edx, eax                            ; 更新最大的内存容量
	.next_ards:
	loop .find_max_mem_area
	jmp .mem_get_ok

	; 获取物理内存大小，最大支持4G
	; int 15h, ax = E801h
	; 返回后，ax与cx值一样，以KB为单位，bx与dx值一样，以64KB为单位，
	; 在ax和cx寄存器中为低16MB，在bx和dx寄存器中为16MB到4GB
	.e820_failed_so_try_e801:
	mov ax, 0xe801
	int 0x15
	jc .e801_failed_so_try_88               ; 若当前e801方法失败，则尝试0x88号子功能
	
	; 1. 先算出低15MB的内存，转换为byte单位
	mov cx, 0x400
	mul cx                                  ; 16位乘法，默认的被乘数是ax，积位32位，高16位在dx中，低16位在ax中
	shl edx, 16
	and eax, 0x0000FFFF
	or edx, eax
	add edx, 0x100000                       ; ax只是15MB，故要加1MB
	mov esi, edx                            ; 先把低15MB的内存容量存入esi寄存器备份

	; 2. 再将16MB以上的内存转换为byte为单位
	xor eax, eax
	mov ax, bx
	mov ecx, 0x10000                        ; 0x10000为十进制64KB
	mul ecx                                 ; 32位乘法，默认的被乘数是eax，积为64位，高32位存入edx，低32位存入eax
	add esi, eax                            ; 由于此方法只能检测出4GB以内的内存，故32位eax足够了，edx肯定为0
	mov edx, esi                            ; edx为总内存大小
	jmp .mem_get_ok

	; 获取物理内存大小，只能获取64MB以内
	; int 15h, ah = 88h
	; 返回后，ax存以KB为单位的内存容量
	.e801_failed_so_try_88:
	mov ah, 0x88
	int 0x15
	jc .error_hlt
	and eax, 0x0000FFFF
	mov cx, 0x400
	mul cx
	shl edx, 16
	or edx, eax
	add edx, 0x100000                       ; 0x88子功能只会返回1MB以上的内存，故实际的内存大小要加上1MB

	.mem_get_ok:
	mov [total_mem_bytes], edx              ; 将内存转换为byte单位后存入total_mem_bytes处
	
	; 准备进入保护模式
	; 1. 打开A20
	; 2. 加载gdt
	; 3. 将cr0的pe位置1

	; 1. 打开A20
	in al, 0x92
	or al, 0000_0010B
	out 0x92, al

	; 2. 加载GDT
	lgdt [gdt_ptr]

	; 3. 将cr0的pe位（第0位）置1
	mov eax, cr0
	or eax, 0x00000001
	mov cr0, eax

	jmp dword SELECTOR_CODE:p_mode_start    ; 刷新流水线

	.error_hlt:                             ; 出错，处理器进入暂停处理命令状态
	hlt

[bits 32]
	p_mode_start:
	mov ax, SELECTOR_DATA
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov esp, LOADER_STACK_TOP
	mov ax, SELECTOR_VIDEO
	mov gs, ax

	; 加载kernel
	mov eax, KERNEL_START_SECTOR            ; 硬盘上kernel.bin所在的扇区号：0x9
	mov ebx, KERNEL_BIN_BASE_ADDR           ; 写入到内存中的物理地址：0x70000
	mov ecx, 200                            ; 写入200个扇区
	call rd_disk_m_32 

	; 准备打开CPU分页
	; 1. 准备好页目录表及页表
	; 2. 将页表地址写入控制寄存器cr3
	; 3. 寄存器cr0的PG位置1

	;1. 创建页目录表及页表并初始化页内存位图
	call setup_page

	; 将全局描述符表地址及偏移量写入内存gdt_ptr，之后用新地址重新加载
	sgdt [gdt_ptr]

	; 将gdt描述符中视频段描述符中的段基址+0xc0000000
	mov ebx, [gdt_ptr + 2]
	; 视频段是第3个段描述符，每个描述符是8字节，故为0x18，段描述符的高4字节的最高8位是段基址的第31-24位
	or dword [ebx + 0x18 + 0x4], 0xc0000000

	; 将gdt的基址加上0xc0000000使其成为内核所在的高地址
	add dword [gdt_ptr + 2], 0xc0000000

	; 将栈指针同样映射到内核地址
	add esp, 0xc0000000

	; 2. 把页目录地址赋给cr3
	mov eax, PAGE_DIR_TABLE_POS
	mov cr3, eax

	; 3. 打开cr0的pg位（第31位）
	mov eax, cr0
	or eax, 0x80000000
	mov cr0, eax

	; 分页已开启，所有寻址以页模式进行

	; 在开启分页后，重新加载gdt新地址
	lgdt [gdt_ptr]

	; 刷新流水线，此处不刷新也没问题
	jmp SELECTOR_CODE:enter_kernel
	enter_kernel:
	call kernel_init            ; 内核被加载到内存后，loader分析其elf结构并将其展开到新的位置，展开后的映像才是真正运行的内核
	mov esp, 0xc009f000         ; 修改栈指针，保持4KB对齐
	; 跳转到内核代码的入口地址执行，最后一棒由内核接管
	jmp KERNEL_ENTRY_POINT      ; 用地址0xc0001500访问测试，结果ok

; rd_disk_m_32：读取硬盘n个扇区（32位下） 
; -----------------------------------------------------------
; eax：LBA扇区号
; ebx：将数据写入的内存地址
; ecx：读入的扇区数
; -----------------------------------------------------------
rd_disk_m_32:
	mov esi, eax             ; 备份eax
	mov di, cx               ; 备份cx

	; 第1步：设置要读取的扇区数
	mov dx, 0x1f2            ; 设置要读写的端口号
	mov al, cl
	out dx, al               ; 向0x1f2端口写入要读取的扇区数
	mov eax, esi             ; 恢复ax

	; 第2步：将LBA地址存入0x1f3-0x1f6

	; LBA地址7-0位写入端口0x1f3
	mov dx, 0x1f3
	out dx, al

	; LBA地址15-8位写入端口0x1f4
	mov cl, 8
	shr eax, cl              ; 逻辑右移
	mov dx, 0x1f4
	out dx, al

	; LBA地址23-16位写入端口0x1f5
	shr eax, cl
	mov dx, 0x1f5
	out dx, al

	shr eax, cl
	and al, 0x0f             ; 设置LBA地址24-27位
	or al, 0xe0              ; 设置7-4位为1110，表示LBA模式
	mov dx, 0x1f6
	out dx, al

	; 第3步：向0x1f7端口写入读命令，0x20
	mov dx, 0x1f7
	mov al, 0x20
	out dx, al

	; 第4步：检测硬盘状态
	; 使用同一端口0x1f7，写时表示写入命令字，读时表示读入硬盘状态
	.not_ready:
	nop                      ; 什么也不做，目的是为了减少对硬盘的打扰
	in al, dx
	and al, 0x88             ; 第3位为1表示硬盘控制器已经准备好进行数据传输，第7位为1表示硬盘忙
	cmp al, 0x08
	jnz .not_ready           ; 若未准备好，继续等待

	; 第5步：通过0x1f0端口读取数据
	; di为要读取的扇区数，一个扇区512字节，每次读入一个字，一共需要读取512 * di / 2次
	mov ax, di               ; 寄存器ax存放需要读取的扇区数
	mov dx, 256
	mul dx                   ; 计算256 * 扇区数
	mov cx, ax               ; 乘积存放在寄存器cx中，作为循环次数

	mov dx, 0x1f0
	
	.go_on_read:
	in ax, dx
	mov [ebx], ax            ; 改用ebx（16位下使用bx）
	add ebx, 2               ; 改用ebx（16位下使用bx） 
	loop .go_on_read

	; 返回调用函数的下一条指令继续执行
	ret




; 将kernel.bin中的segment拷贝到编译的地址
; -----------------------------------------------------------
kernel_init:
	xor eax, eax                
	xor ebx, ebx                     ; ebx记录程序头表地址
	xor ecx, ecx                     ; cx记录程序头表中条目的数量，实际上就是段的数量
	xor edx, edx                     ; dx记录程序头表中每个条目（program header）的字节大小，即用来描述段信息的数据结构的字节大小

	; 偏移文件42字节处的属性是e_phentsize，表示每个program header的字节大小
	mov dx, [KERNEL_BIN_BASE_ADDR + 42]
	; 偏移文件28字节处的属性是e_phoff，表示第1个program header在文件中的偏移量
	mov ebx, [KERNEL_BIN_BASE_ADDR + 28]
	add ebx, KERNEL_BIN_BASE_ADDR
	; 偏移文件44字节处是e_phnum，表示有几个program header
	mov cx, [KERNEL_BIN_BASE_ADDR + 44]

	; 拷贝每一个段（segment）
	.each_segment:
	cmp byte [ebx + 0], PT_NULL
	je .PTNULL                       ; 若程序段类型等于PT_NULL，说明该program header未使用
	; 调用函数memcpy的准备工作：为函数memcpy压入参数，参数从右往左依次压入，函数原型为memcpy(dst, src, size)
	; 压入函数memcpy的第三个参数：size
	push dword [ebx + 16]            ; program header中偏移16字节处是p_filesz
	mov eax, [ebx + 4]               ; program header中偏移4字节处是p_offset
	add eax, KERNEL_BIN_BASE_ADDR    ; 加上kernel.bin被加载到的物理地址，eax为该段的物理地址
	; 压入函数memcpy的第二个参数：src
	push eax
	; 压入函数memcpy的第一个参数：dst
	push dword [ebx + 8]             ; program header中偏移8字节处是p_vaddr，表示本段在内存中的起始虚拟地址
	; 调用mem_cpy完成段复制
	call mem_cpy
	add esp, 12                      ; 清理栈中压入的三个参数
	.PTNULL:
	add ebx, edx                     ; edx为program header大小，即e_phentsize，在此使ebx指向下一个program header
	loop .each_segment
	ret

; 逐字节拷贝：mem_cpy(dst, src, size) 
; -----------------------------------------------------------
; 输入：栈中三个参数（dst, src, size）
; 输出：无
; -----------------------------------------------------------
mem_cpy:
	cld                              ; 控制方向标志位DF，cld使DF置0，表示向高地址方向增加
	push ebp
	mov ebp, esp
	push ecx                         ; rep指令用到了ecx，但ecx在外层循环已经使用，所以先入栈备份
	mov edi, [ebp + 8]               ; 取参数dst
	mov esi, [ebp + 12]              ; 取参数src
	mov ecx, [ebp + 16]              ; 取参数size
	rep movsb                        ; 逐字节拷贝

	; 恢复环境
	pop ecx
	pop ebp
	ret

; 创建页目录表及页表
; -----------------------------------------------------------

setup_page:
	; 先把页目录表占用的空间逐字节清0
	mov ecx, 4096
	mov esi, 0
	.clear_page_dir:
	mov byte [PAGE_DIR_TABLE_POS + esi], 0
	inc esi
	loop .clear_page_dir

	; 开始创建页目录项（PDE）
	.create_pde:
	mov eax, PAGE_DIR_TABLE_POS
	add eax, 0x1000                          ; 此时eax为第一个页表的位置
	mov ebx, eax                             ; 此处为ebx赋值，是为.create_pte做准备，ebx为基址

	; 下面将页目录项0x0和0xc00都存为第一个页表的地址，每个页表表示4MB内存，
	; 这样0xc03fffff以下的地址和0x003fffff以下的地址都指向相同的页表，这是为将地址映射为内核地址做准备
	or eax, PG_US_U | PG_RW_W | PG_P         ; 此时eax为第一个页表的位置和属性
	                                         ; 页目录项的属性RW和P位为1，US为1，表示用户属性，所有特权级别都可以访问
	mov [PAGE_DIR_TABLE_POS + 0x0], eax      ; 在页目录表中的第1个目录项写入第一个页表的位置（0x101000）及属性（7）
	mov [PAGE_DIR_TABLE_POS + 0xc00], eax    ; 在页目录表中0xc00对应的目录项写入第一个页表的位置（0x101000）及属性（7）
	                                         ; 0xc00表示第768个页表占用的目录项，0xc00以上的目录项用于内核空间，
	                                         ; 也就是0xc0000000-0xffffffff共计1G属于内核，0x0-0xbfffffff共计3G属于用户进程
	sub eax, 0x1000
	mov [PAGE_DIR_TABLE_POS + 4092], eax     ; 使最后一个目录项指向页目录表自己的位置

	; 下面创建页表项（PTE），目前只建立1M低端物理内存的映射，也就是256个物理页
	mov ecx, 256                             ; 1M低端内存 / 每页大小4K = 256
	mov esi, 0
	mov edx, PG_US_U | PG_RW_W | PG_P        ; 页表项属性为7，US=1 RW=1 P=1
	.create_pte:
	mov [ebx + esi * 4], edx                 ; ebx之前已经被赋值为0x101000，也就是第一个页表的地址
	add edx, 4096
	inc esi
	loop .create_pte

	; 创建内核其他页表的PDE，前256个页表都是属于内核的（256 * 4M = 1G）
	mov eax, PAGE_DIR_TABLE_POS
	add eax, 0x2000                          ; 此时eax为第二个页表的位置
	or eax, PG_US_U | PG_RW_W | PG_P         ; 页目录项的属性US、RW和P位都为1
	mov ebx, PAGE_DIR_TABLE_POS
	mov ecx, 254                             ; 范围为第769-1022的所有目录项数量
	mov esi, 769
	.create_kernel_pde:
	mov [ebx + esi * 4], eax
	inc esi
	add eax, 0x1000
	loop .create_kernel_pde
	ret
