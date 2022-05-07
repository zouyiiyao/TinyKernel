# 此脚本应该在command目录下执行

if [[ ! -d "../lib" || ! -d "../build" ]]; then
	echo "dependent dir do not exist!"
	cwd=$(pwd)
	cwd=${cwd##*/}
	cwd=${cwd%/}
	if [[ $cwd != "command" ]]; then
		echo -e "you'd better in command dir\n"
	fi
	exit
fi

BIN="prog_arg"
CFLAGS="-Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers -m32"
LIBS="-I ../lib/ -I ../lib/user -I ../fs -I ../thread -I ../device -I ../lib/kernel -I ../kernel"
OBJS="../build/string.o ../build/syscall.o ../build/stdio.o ../build/assert.o start.o"
DD_IN=$BIN
DD_OUT="/home/zouyi/Desktop/os/bochs/bin/hd60M.img"

nasm -f elf ./start.S -o ./start.o
ar rcs simple_crt.a $OBJS start.o

gcc $CFLAGS $LIBS -o $BIN".o" $BIN".c"
ld -e _start -m elf_i386 $BIN".o" simple_crt.a -o $BIN

SEC_CNT=$(ls -l $BIN|awk '{printf("%d", ($5+511)/512)}') # SEC_CNT=13
if [[ -f $BIN ]]; then
	dd if=./$DD_IN of=$DD_OUT bs=512 count=$SEC_CNT seek=300 conv=notrunc
fi
