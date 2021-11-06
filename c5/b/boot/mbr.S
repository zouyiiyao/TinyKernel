; 主引导程序
; -----------------------------------------------------------

%include "boot.inc"

SECTION MBR vstart=0x7c00
	mov ax, cs
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov fs, ax
	mov sp, 0x7c00
	mov ax, 0xb800    ; 显存段
	mov gs, ax

; 清屏利用0x06号功能，上卷全部行，则可清屏。
; -----------------------------------------------------------
; INT 0x10  功能号：0x06  功能描述：上卷窗口
; -----------------------------------------------------------
; 输入：
; AH = 0x06（功能号）
; AL = 上卷的行数（如果为0，表示全部）
; BH = 上卷行属性
; (CL, CH) = 窗口左下角的(X, Y)位置
; (DL, DH) = 窗口右下角的(X, Y)位置
; 返回值：
; 无
	mov ax, 0x600
	mov bx, 0x700
	mov cx, 0         ; 左上角：(0, 0)
	mov dx, 0x184f    ; 右上角：(80, 25)，
                          ; VGA文本模式中，一行只能容纳80个字符，共25行。
                          ; 下标从0开始，0x18=24，0x4f=79
	int 0x10          ; int 0x10


;;; 打印字符串 ;;;
	; 输出背景色绿色，前景色红色，并且跳动的字符串"1 MBR"
	mov byte [gs:0x00], '1'
	mov byte [gs:0x01], 0xA4    ; A表示绿色背景闪烁，4表示前景色为红色
	
	mov byte [gs:0x02], ' '
	mov byte [gs:0x03], 0xA4

	mov byte [gs:0x04], 'M'
	mov byte [gs:0x05], 0xA4

	mov byte [gs:0x06], 'B'
	mov byte [gs:0x07], 0xA4

	mov byte [gs:0x08], 'R'
	mov byte [gs:0x09], 0xA4
;;; 打印字符串结束 ;;;

;;; 读取硬盘上的loader到内存中 ;;;
	mov eax, LOADER_START_SECTOR    ; 传入参数，loader起始扇区LBA扇区号
	mov bx, LOADER_BASE_ADDR        ; 传入参数，写入的物理内存地址
	mov cx, 4                       ; 传入参数，待读入的扇区数
	call rd_disk_m_16               ; 调用函数，读取loader程序（四个扇区）
;;; 读取结束 ;;;
	
	jmp LOADER_BASE_ADDR            ; 跳转到loader程序第一条指令开始执行

; rd_disk_m_16函数定义
; -----------------------------------------------------------
; 功能：读取硬盘n个扇区
; 参数：eax=LBA扇区号
;       bx=将数据写入的内存地址
;       cx=读入的扇区数
; -----------------------------------------------------------
rd_disk_m_16:
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
	mov [bx], ax
	add bx, 2
	loop .go_on_read

	; 返回调用函数的下一条指令继续执行
	ret

; rd_disk_m_16函数定义结束

	times 510-($-$$) db 0    ; $表示当前行的虚拟地址，$$表示当前section的虚拟起始地址，
                                 ; 目的是为了填充满512字节的主引导扇区
	db 0x55, 0xaa            ; 主引导扇区的标志，最后两个字节必须是0x55 & 0xaa
