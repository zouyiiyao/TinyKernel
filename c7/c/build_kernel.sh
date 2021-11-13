#!/bin/bash

# 编译print.S
nasm -f elf -o build/print.o lib/kernel/print.S

# 编译kernel.S
nasm -f elf -o build/kernel.o kernel/kernel.S

# 编译、汇编到目标代码，不进行链接，也就是直接生成目标文件，-m32指生成32位目标文件
gcc -I lib/kernel/ -I kernel/ -c -fno-builtin -o build/main.o kernel/main.c -m32

gcc -I lib/kernel/ -c -o build/timer.o device/timer.c -m32

gcc -I lib/kernel/ -I kernel/ -c -fno-builtin -o build/interrupt.o kernel/interrupt.c -m32

gcc -I lib/kernel/ -I kernel/ -c -fno-builtin -o build/init.o kernel/init.c -m32

# 链接，完成符号的重定位工作（编排地址）
ld -Ttext 0xc0001500 -e main -o build/kernel.bin -m elf_i386 build/main.o build/init.o \
	build/interrupt.o build/print.o build/kernel.o build/timer.o

# 将内核文件写入硬盘的9号扇区，loader将其从硬盘读出并加载到内存中，至此操作系统接管
dd if=build/kernel.bin of=/home/zouyi/Desktop/os/bochs/bin/hd60M.img bs=512 count=200 seek=9 conv=notrunc
