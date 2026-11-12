# GRID OS stage-1 bootloader.
# Loaded by BIOS at 0000:7C00. Captures the e820 memory map, loads the
# kernel from disk (LBA 1..62) to 0000:8000, enters 32-bit protected mode
# and jumps to the kernel entry point.
#
# Memory layout:
#   0x04FE  word    e820 entry count
#   0x0500  24*B    e820 entries
#   0x7C00  512B    this bootloader
#   0x8000  ..      kernel image (max 62 sectors = 31744 bytes)

    .intel_syntax noprefix
    .code16
    .globl _start

_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    # ---- capture e820 memory map into 0x0500 ----
    mov di, 0x0500
    mov word ptr [e820_count], 0
e820_loop:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    jc e820_done
    cmp eax, 0x534D4150
    jne e820_done
    add di, 24
    inc word ptr [e820_count]
    cmp word ptr [e820_count], 24
    jae e820_done
    test ebx, ebx
    jnz e820_loop
e820_done:

    # ---- try int 13h extensions (LBA read) ----
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc chs_read
    cmp bx, 0xAA55
    jne chs_read

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, offset dap
    int 0x13
    jnc pm_enter

    # ---- fallback: CHS read, sectors 2..63, head 0, cyl 0 ----
chs_read:
    mov ax, 0x0000
    mov es, ax
    mov bx, 0x8000
    mov ah, 0x02
    mov al, 62
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_err

    # ---- enter protected mode ----
pm_enter:
    cli
    in al, 0x92
    or al, 0x02
    out 0x92, al
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    .att_syntax
    ljmp $0x08, $pm32
    .intel_syntax noprefix

pm32:
    .code32
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x7C00
    jmp 0x8000

disk_err:
    mov si, offset err_msg
p_err:
    lodsb
    or al, al
    jz halt
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp p_err
halt:
    hlt
    jmp halt

    .align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_desc:
    .word gdt_desc - gdt - 1
    .long gdt

boot_drive:  .byte 0
e820_count:  .word 0
dap:
    .word 16
    .word 62
    .word 0x8000
    .word 0x0000
    .long 1
    .long 0
err_msg: .asciz "GRID: disk read error"

    .org 510
    .word 0xAA55
