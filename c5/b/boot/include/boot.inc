; -----------------------------------------------------------
; loader config
; -----------------------------------------------------------

LOADER_BASE_ADDR    equ 0x900     ; 定义loader在内存中的位置，MBR将loader从硬盘从读入后放在物理地址0x900处
LOADER_START_SECTOR equ 0x2       ; 定义loader在硬盘上的逻辑扇区地址，即LBA地址，loader被放置在硬盘的第2块扇区

PAGE_DIR_TABLE_POS  equ 0x100000  ; 定义页目录表起始物理地址

;;; gdt描述符属性 ;;;

DESC_G_4K           equ 1_00000000000000000000000b    ; 段界限的单位大小为4K
DESC_D_32           equ  1_0000000000000000000000b    ; 指令中有效地址及操作数为32位，指令有效地址用eip寄存器
DESC_L              equ   0_000000000000000000000b    ; 64位代码标记，此处标记为0即可
DESC_AVL            equ    0_00000000000000000000b    ; CPU不用此位，暂置为0
DESC_LIMIT_CODE2    equ     1111_0000000000000000b    ; 段界限19-16
DESC_LIMIT_DATA2    equ     DESC_LIMIT_CODE2
DESC_LIMIT_VIDEO2   equ     0000_0000000000000000b
DESC_P              equ         1_000000000000000b    ; 段出现在内存中的标志，置1

; DPL字段，段描述符所代表内存段的特权级
DESC_DPL_0          equ          00_0000000000000b    
DESC_DPL_1          equ          01_0000000000000b
DESC_DPL_2          equ          10_0000000000000b
DESC_DPL_3          equ          11_0000000000000b

; S字段，标识是否为系统段
DESC_S_CODE         equ            1_000000000000b
DESC_S_DATA         equ            DESC_S_CODE
DESC_S_sys          equ            0_000000000000b

; TYPE字段，根据S字段的取值，指示门/段的类型
DESC_TYPE_CODE      equ             1000_00000000b    ; x=1, c=0, r=0, a=0 代码段是可执行的，非一致性，不可读，已访问位a清0
DESC_TYPE_DATA      equ             0010_00000000b    ; x=0, e=0, w=1, a=0 数据段是不可执行的，向上扩展的，可写，已访问位a清0

DESC_CODE_HIGH4     equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + \
DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + \
DESC_P + DESC_DPL_0 + DESC_S_CODE + \
DESC_TYPE_CODE + 0x00

DESC_DATA_HIGH4     equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + \
DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + \
DESC_P + DESC_DPL_0 + DESC_S_DATA + \
DESC_TYPE_DATA + 0x00

DESC_VIDEO_HIGH4    equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + \
DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + \
DESC_P + DESC_DPL_0 + DESC_S_DATA + \
DESC_TYPE_DATA + 0x0b    ; 这里书上的代码有错误，应该是0x0b而不是0x00

;;; 段选择子属性 ;;;
RPL0                equ 00b
RPL1                equ 01b
RPL2                equ 10b
RPL3                equ 11b
TI_GDT              equ 000b
TI_LDT              equ 100b

;;; 页目录/页表相关属性 ;;;
PG_P                equ 1b
PG_RW_R             equ 00b
PG_RW_W             equ 10b
PG_US_S             equ 000b
PG_US_U             equ 100b
