[bits 32]
%define ERROR_CODE nop    ; 若在相关的异常中CPU已经自动压入了错误码，为保持格式统一，这里不做操作
%define ZERO push 0       ; 若在相关的异常中CPU没有压入错误码，为统一栈中格式，这里手工压入一个0

extern put_str            ; 声明外部函数

section .data             ; 0xa为换行符
intr_str db "interrupt occur!", 0xa, 0

; 导出为全局数组
global intr_entry_table

intr_entry_table:

; 含两个参数的宏定义
%macro VECTOR 2

section .text
intr%1entry:
	%2                ; 统一栈中格式
	push intr_str     ; 参数入栈
	call put_str      ; 调用字符串打印函数
	add esp, 4        ; 回收参数空间

	; 如果是从片上进入的中断，除了往从片上发送EOI外，还要往主片上发送EOI
	mov al, 0x20      ; 主动发送中断结束命令EOI
	out 0xa0, al      ; 向从片发送
	out 0x20, al      ; 向主片发送

	add esp, 4        ; 跳过error_code
	iret              ; 从中断返回

section .data
	dd intr%1entry    ; 存储各个中断入口程序的地址，形成intr_entry_table数组

%endmacro

; 定义33个中断处理程序（宏展开，用类似数组的方式访问）
; 由于中断向量号0-19为处理器内部固定的异常类型，20-31为intel保留，所以我们可用的中断向量号最低是32
VECTOR 0x00, ZERO
VECTOR 0x01, ZERO
VECTOR 0x02, ZERO
VECTOR 0x03, ZERO
VECTOR 0x04, ZERO
VECTOR 0x05, ZERO
VECTOR 0x06, ZERO
VECTOR 0x07, ZERO
VECTOR 0x08, ERROR_CODE
VECTOR 0x09, ZERO
VECTOR 0x0a, ERROR_CODE
VECTOR 0x0b, ERROR_CODE
VECTOR 0x0c, ZERO
VECTOR 0x0d, ERROR_CODE    ; GP异常
VECTOR 0x0e, ERROR_CODE    ; PF异常
VECTOR 0x0f, ZERO
VECTOR 0x10, ZERO
VECTOR 0x11, ERROR_CODE
VECTOR 0x12, ZERO
VECTOR 0x13, ZERO 
VECTOR 0x14, ZERO
VECTOR 0x15, ZERO
VECTOR 0x16, ZERO
VECTOR 0x17, ZERO 
VECTOR 0x18, ERROR_CODE
VECTOR 0x19, ZERO
VECTOR 0x1a, ERROR_CODE
VECTOR 0x1b, ERROR_CODE 
VECTOR 0x1c, ZERO
VECTOR 0x1d, ERROR_CODE
VECTOR 0x1e, ERROR_CODE
VECTOR 0x1f, ZERO 
VECTOR 0x20, ZERO
