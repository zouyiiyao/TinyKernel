[bits 32]
section .text
global switch_to
switch_to:
	; 栈中此处为返回地址
	push esi
	push edi
	push ebx
	push ebp

	mov eax, [esp + 20]    ; 取栈中参数cur
	mov [eax], esp         ; 保存栈顶指针，cur->self_kstack = esp

	mov eax, [esp + 24]    ; 取栈中参数next
	mov esp, [eax]         ; 恢复到next的0级栈顶指针，esp = next->self_kstack

	pop ebp
	pop ebx
	pop edi
	pop esi
	; 返回到调用switch_to的下一条指令执行，第一次执行会返回到kernel_thread
	ret 
