TI_GDT         equ 0
RPL0           equ 0
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

section .data
put_int_buffer dq 0               ; 定义8字节缓冲区用于数字到字符的转换

[bits 32]
section .text

global put_str

; put_str
; -----------------------------------------------------------
; 功能描述：put_str通过put_char来打印以0字符结尾的字符串
; 输入：栈中参数为打印的字符串地址
; 输出：无
; -----------------------------------------------------------
put_str:
	; 由于本函数中只用到了ebx和ecx，只备份这两个寄存器
	push ebx
	push ecx
	xor ecx, ecx              ; 准备用ecx存储参数，清空
	mov ebx, [esp + 12]       ; 从栈中得到待打印的字符串地址
	.goon:
	mov cl, [ebx]
	cmp cl, 0                 ; 如果处理到了字符串尾，跳到结束处返回
	jz .str_over
	push ecx                  ; 为put_char函数传递参数
	call put_char
	add esp, 4                ; 回收参数所占的栈空间
	inc ebx                   ; 使ebx指向下一个字符
	jmp .goon
	.str_over:
	pop ecx
	pop ebx
	ret 
	 
global put_char

; put_char
; -----------------------------------------------------------
; 功能描述：把栈中的1个字符写入光标所在处 
; -----------------------------------------------------------
put_char:
	pushad                    ; 备份32位寄存器环境
	; 需要保证gs中为正确的视频段选择子，为保险起见，每次打印时都为gs赋值
	mov ax, SELECTOR_VIDEO    ; 不能直接把立即数送入段寄存器
	mov gs, ax

	; 获取当前光标位置
	; 先获取高8位
	mov dx, 0x03d4            ; 先操作索引寄存器
	mov al, 0x0e              ; 用于提供光标位置高8位的索引值
	out dx, al                ; 将索引值写入索引寄存器
	mov dx, 0x03d5            ; 通过读写数据端口0x3d5来获得或设置光标位置
	in al, dx                 ; 得到了光标位置的高8位
	mov ah, al                ; 存在ah中

	; 再获取低8位
	mov dx, 0x03d4
	mov al, 0x0f
	out dx, al
	mov dx, 0x03d5
	in al, dx

	; 将光标位置存在bx中
	mov bx, ax
	; 下面这行是在栈中获取待打印的字符
	mov ecx, [esp + 36]       ; 主调函数4字节的返回地址，加上pushad压入8个双字寄存器32字节
	cmp cl, 0xd               ; 回车符（CR）是0x0d
	jz .is_carriage_return
	cmp cl, 0xa               ; 换行符（LF）是0x0a
	jz .is_line_feed

	cmp cl, 0x8               ; 退格符（BS）是0x8
	jz .is_backspace
	jmp .put_other

	; 退格字符处理逻辑
	; 说明：当为backspace时，本质上只要将光标移向前一个显存位置，后面再输入的字符自然会覆盖此处的字符，
	; 但有可能在键入backspace后并不再键入其他字符，这时光标向前移动到待删除的字符位置，但字符还在原处，
	; 这就显得怪异，因此在此处用空格（0x20）来填充待删除的字符
	.is_backspace:
	dec bx
	shl bx, 1                 ; 光标左移1位，表示光标对应显存中的偏移字节
	mov byte [gs:bx], 0x20    ; 将待删除的字节补为空格
	inc bx
	mov byte [gs:bx], 0x07
	shr bx, 1
	jmp .set_cursor

	; 可打印字符处理逻辑
	.put_other:
	shl bx, 1
	mov [gs:bx], cl           ; ASCII字符本身
	inc bx
	mov byte [gs:bx], 0x07    ; 字符属性
	shr bx, 1                 ; 恢复为光标值
	inc bx                    ; 下一个光标值
	cmp bx, 2000
	jl .set_cursor            ; 若光标值小于2000，表示未写到屏幕显示范围的最后，则去设置新的光标值，
	                          ; 若超出屏幕字符数大小（2000），则换行处理

	; 回车或换行符处理逻辑
	.is_line_feed:            ; 是换行符LF（\n）
	.is_carriage_return:      ; 是回车符CR（\r）
	xor dx, dx                ; dx是被除数的高16位，清0
	mov ax, bx                ; ax是被除数的低16位
	mov si, 80                ; 由于是效仿Linux，Linux中\n表示下一行的行首，
	                          ; 所以本系统中将\n和\r都处理为Linux中\n的意思
	div si
	sub bx, dx                ; 光标值减去除80的余数便是取整

	; 回车或换行处理收尾
	add bx, 80                ; 光标移动到下一行行首
	cmp bx, 2000              ; 判断光标值是否小于2000
	.is_line_feed_end:
	jl .set_cursor            ; 若光标值小于2000，表示未写到屏幕显示范围的最后，则去设置新的光标值

	; 滚屏操作
	; 屏幕行范围是0-24，滚屏的原理是将屏幕的第1-24行搬运到第0-23行
	.roll_screen:
	cld
	mov ecx, 960              ; 2000-80=1920个字符要搬运，共1920*2=3840字节，一次搬运4字节，共3840/4=960次
	mov esi, 0xc00b80a0       ; 第1行行首
	mov edi, 0xc00b8000       ; 第0行行首
	rep movsd

	; 将最后一行填充为空白
	mov ebx, 3840             ; 最后一行首字符的第一个字节偏移
	mov ecx, 80
	.cls:
	; 0x0720是黑底白字的空格键
	mov word [gs:ebx], 0x0720
	add ebx, 2
	loop .cls
	mov bx, 1920              ; 将光标值重置为1920，最后一行的首字符

	; 将光标设为bx值
	.set_cursor:
	; 先设置高8位
	mov dx, 0x03d4            ; 先操作索引寄存器
	mov al, 0x0e              ; 用于设置光标位置的高8位索引值
	out dx, al                ; 将索引值写入索引寄存器
	mov dx, 0x03d5            ; 通过读写数据端口0x03d5来获得或设置光标位置
	mov al, bh
	out dx, al

	; 再设置低8位
	mov dx, 0x03d4
	mov al, 0x0f
	out dx, al
	mov dx, 0x03d5
	mov al, bl
	out dx, al

	.put_char_done:
	popad
	ret

global cls_screen

cls_screen:
	pushad
	; 由于用户进程的cpl为3，显存段的dpl为0，故用于显存的段选择子gs在低于自己特权级的环境中为0，
	; 导致用户进程再次进入中断后gs为0，所以每次都为gs赋值
	mov ax, SELECTOR_VIDEO       ; 不能直接把立即数送入gs，需要由ax中转
	mov gs, ax

	mov ebx, 0
	mov ecx, 80 * 25

	.cls:
	mov word [gs:ebx], 0x0720    ; 0x0720是黑底白字的空格键
	add ebx, 2
	loop .cls
	mov ebx, 0

	.set_cursor:
	; 先设置高8位
	mov dx, 0x03d4            ; 先操作索引寄存器
	mov al, 0x0e              ; 用于设置光标位置的高8位索引值
	out dx, al                ; 将索引值写入索引寄存器
	mov dx, 0x03d5            ; 通过读写数据端口0x03d5来获得或设置光标位置
	mov al, bh
	out dx, al

	; 再设置低8位
	mov dx, 0x03d4
	mov al, 0x0f
	out dx, al
	mov dx, 0x03d5
	mov al, bl
	out dx, al

	popad
	ret

global put_int

; put_int
; -----------------------------------------------------------
; 功能描述：将小端字节序的数字变成对应的ASCII后，倒置
; 输入：栈中参数为待打印的数字
; 输出：在屏幕上打印十六进制数字，并不会打印前缀0x
; -----------------------------------------------------------
put_int:
	pushad
	mov ebp, esp
	mov eax, [ebp + 4 * 9]    ; call的返回地址4字节 + pushad的8个4字节
	mov edx, eax
	mov edi, 7                ; 指定在put_int_buffer中初始的偏移量
	mov ecx, 8                ; 32位数字，对应8个十六进制数
	mov ebx, put_int_buffer

	; 将32位数字按照十六进制的形式从低位到高位逐个处理，共处理8个十六进制数字
	.16based_4bits:
	; 遍历每一位十六进制数字
	and edx, 0x0000000F       ; 解析十六进制数字的每一位，and操作后，edx只有低4位有效
	cmp edx, 9                ; 数字0-9和a-f转化为对应的十六进制字符，使用不同的逻辑
	jg .is_A2F
	add edx, '0'
	jmp .store
	.is_A2F:
	sub edx, 10               ; A-F减去10所得到的差，再加上字符'A'的ASCII码，便是A-F对应的ASCII码
	add edx, 'A'

	; 将每一位数字转换为对应的字符后，按照类似大端的顺序存储到缓冲区put_int_buffer，
	; 高位字符放在低地址，低位字符要放在高地址，这样和大端字节序类似，只不过我们这里是字符序
	.store:
	mov [ebx + edi], dl       ; 此时dl中应该是数字对应的字符的ASCII码
	dec edi
	shr eax, 4
	mov edx, eax
	loop .16based_4bits

	; 现在put_int_buffer中已经全是字符，打印之前把高位的'0'字符去掉
	.ready_to_print:
	inc edi                   ; 此时edi退减为-1（0xffffffff），加1使其为0
	
	.skip_prefix_0:
	cmp edi, 8                ; 若已经比较第9个字符了，表示待打印的字符串为全'0'
	je .full0
	; 找出连续的'0'字符，edi作为非0的最高位字符的偏移
	.go_on_skip:
	mov cl, [put_int_buffer + edi]
	inc edi
	cmp cl, '0'
	je .skip_prefix_0         ; 继续判断下一位字符是否为字符'0'
	dec edi                   ; edi在上面的inc操作中指向了下一个字符，若当前字符不为'0'，要使edi减1恢复指向当前字符
	jmp .put_each_num

	.full0:
	mov cl, '0'               ; 输入的数字全为0时，则只打印0
	.put_each_num:
	push ecx                  ; 此时cl中为可打印的字符
	call put_char
	add esp, 4
	inc edi                   ; 使edi指向下一个字符
	mov cl, [put_int_buffer + edi]
	cmp edi, 8
	jl .put_each_num
	popad
	ret

global set_cursor
set_cursor:
	pushad

	mov bx, [esp + 36]
	; 先设置高8位
	mov dx, 0x03d4            ; 先操作索引寄存器
	mov al, 0x0e              ; 用于设置光标位置的高8位索引值
	out dx, al                ; 将索引值写入索引寄存器
	mov dx, 0x03d5            ; 通过读写数据端口0x03d5来获得或设置光标位置
	mov al, bh
	out dx, al

	; 再设置低8位
	mov dx, 0x03d4
	mov al, 0x0f
	out dx, al
	mov dx, 0x03d5
	mov al, bl
	out dx, al

	popad
	ret

