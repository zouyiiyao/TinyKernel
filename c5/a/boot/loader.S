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

	mov byte [gs:160], 'P'      ; 每行80字符，每个字符占2字节 
	
	jmp $                       ; 通过死循环让程序悬停在此
