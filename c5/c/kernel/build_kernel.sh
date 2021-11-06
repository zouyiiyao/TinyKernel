#!/bin/bash

# 编译、汇编到目标代码，不进行链接，也就是直接生成目标文件，-m32指生成32位目标文件
gcc -c -o main.o main.c -m32

# 链接，完成符号的重定位工作（编排地址）
ld main.o -Ttext 0xc0001500 -e main -o kernel.bin -m elf_i386

# 将内核文件写入硬盘的9号扇区，loader将其从硬盘读出并加载到内存中，至此操作系统接管
dd if=kernel.bin of=/home/zouyi/Desktop/os/bochs/bin/hd60M.img bs=512 count=200 seek=9 conv=notrunc
