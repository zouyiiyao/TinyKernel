; 内核加载程序
; -----------------------------------------------------------

%include "boot.inc"

section loader vstart=LOADER_BASE_ADDR

LOADER_STACK_TOP equ LOADER_BASE_ADDR
	jmp loader_start

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

	; 段选择子
	SELECTOR_CODE  equ (0x0001<<3) + TI_GDT + RPL0    ; 相当于(CODE_DESC - GDT_BASE) / 8 + TI_GDT + RPL0
	SELECTOR_DATA  equ (0x0002<<3) + TI_GDT + RPL0    ; 相当于(DATA_DESC - GDT_BASE) / 8 + TI_GDT + RPL0
	SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0    ; 相当于(VIDEO_DESC - GDT_BASE) / 8 + TI_GDT + RPL0

	; 以下是gdt的指针，前2字节是gdt界限，后4字节是gdt起始地址
	gdt_ptr dw GDT_LIMIT
	        dd GDT_BASE
	loadermsg db '2 loader in real.'
	
	loader_start:
	; 打印字符串
	mov sp, LOADER_BASE_ADDR
	mov bp, loadermsg      ; ES:BP = 字符串地址
	mov cx, 17             ; CX = 字符串长度
	mov ax, 0x1301         ; AH = 0x13，AL = 0x01
	mov bx, 0x001f         ; 页号为0（BH = 0x00），蓝底粉红字（BL = 0x1f）
	mov dx, 0x1800         ; 坐标最后一行第一个位置
	int 0x10               ; 0x10号中断
	; 打印字符串结束

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
