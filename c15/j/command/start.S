[bits 32]
extern main
extern exit
section .text
global _start

; 用户程序第真正入口
_start:
	; 取出参数压入用户栈，需要和execv中load之后指定的寄存器一致
	push ebx     ; 压入参数argv
	push ecx     ; 压入参数argc
	call main    ; 调用用户程序main函数

	; 将main函数的返回值通过栈传递给exit，gcc用eax存储返回值，这是ABI规定的
	push eax
	call exit    ; exit不会返回
