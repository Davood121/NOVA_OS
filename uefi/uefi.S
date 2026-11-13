/*
 * GRID OS UEFI loader (v0.6) - hand-rolled PE32+ EFI application.
 *
 * No gnu-efi, no EDK2: the PE headers are written by hand below and the
 * image is linked as a flat binary. Layout (file offset == RVA):
 *
 *   0x0000  PE headers            (SizeOfHeaders = 0x200)
 *   0x1000  .text    loader code + data (12 KB reserved)
 *   0x4000  .payload kernel.bin embedded via .incbin
 *
 * The loader: prints progress on the UEFI console, kills the watchdog,
 * locates its own image via the LoadedImage protocol, captures the
 * memory map (with retry on EFI_BUFFER_TOO_SMALL), calls
 * ExitBootServices (with retry, re-reading the map each attempt),
 * copies the kernel to 0x8000, forces VGA text mode 3 by register
 * writes (GOP left the text buffer unusable), then drops from long
 * mode to 32-bit protected mode with the exact GDT layout boot.S uses
 * (CS 0x08 / DS 0x10) and jumps to the kernel entry point.
 */

    .intel_syntax noprefix
    .include "build/uefi/sizes.inc"

    .set IMG_BASE,   0x100000
    .set TEXT_RVA,   0x1000
    .set PL_RVA,     0x4000
    .set KERN_LOAD,  0x8000

/* ---------------- PE32+ headers ---------------- */
    .section .pehdr, "a"
    .ascii "MZ"
    .org 0x3c
    .long 0x40
    .org 0x40
    .ascii "PE"
    .word 0
coff:
    .word 0x8664
    .word 2
    .long 0
    .long 0
    .long 0
    .word 240
    .word 0x0022
opt:
    .word 0x20B
    .byte 1, 0
    .long 0x3000
    .long PAYLOAD_RAW
    .long 0
    .long TEXT_RVA
    .long TEXT_RVA
    .quad IMG_BASE
    .long 0x1000
    .long 0x200
    .word 4, 0
    .word 0, 0
    .word 1, 0
    .long 0
    .long SIZEOF_IMAGE
    .long 0x200
    .long 0
    .word 10
    .word 0
    .quad 0x100000
    .quad 0x1000
    .quad 0x100000
    .quad 0x1000
    .long 0
    .long 16
    .rept 16
    .quad 0
    .endr
sec_text:
    .ascii ".text"
    .byte 0, 0, 0
    .long 0x3000
    .long TEXT_RVA
    .long 0x3000
    .long TEXT_RVA
    .long 0, 0, 0, 0, 0
    .long 0x60000020
sec_payload:
    .ascii ".payload"
    .byte 0, 0, 0
    .long KERNEL_SIZE
    .long PL_RVA
    .long PAYLOAD_RAW
    .long PL_RVA
    .long 0, 0, 0, 0, 0
    .long 0x40000040

/* ---------------- loader code ---------------- */
    .section .text, "ax"
    .balign 16
    .globl _start
_start:
    cli
    cld
    mov r15, rcx
    mov r14, rdx
    and rsp, -16

    lea rax, [rip+s_start]
    mov rcx, [r14+0x40]
    mov rdx, rax
    mov rax, [rcx+8]
    sub rsp, 0x28
    call rax
    add rsp, 0x28

    mov rax, [r14+0x60]
    mov rcx, 0
    mov rdx, 0
    mov r8, 0
    mov r9, 0
    mov rax, [rax+0xF0]
    sub rsp, 0x28
    call rax
    add rsp, 0x28

    mov rax, [r14+0x60]
    mov rcx, r15
    lea rdx, [rip+g_li_guid]
    lea r8, [rip+img_base]
    mov rax, [rax+0x88]
    sub rsp, 0x28
    call rax
    add rsp, 0x28
    test eax, eax
    jnz ebs_fail

    lea rax, [rip+s_image]
    mov rcx, [r14+0x40]
    mov rdx, rax
    mov rax, [rcx+8]
    sub rsp, 0x28
    call rax
    add rsp, 0x28

    mov ebx, 3
ebs_loop:
    call get_mm
    test eax, eax
    jnz ebs_fail

    mov rax, [r14+0x60]
    mov rcx, r15
    mov rdx, [rip+mm_key]
    mov rax, [rax+0xD8]
    sub rsp, 0x28
    call rax
    add rsp, 0x28
    test eax, eax
    jz ebs_ok
    dec ebx
    jnz ebs_loop
    jmp ebs_fail

ebs_ok:
    mov rsi, [rip+img_base]
    mov eax, payload_start - IMG_BASE
    add rsi, rax
    mov edi, KERN_LOAD
    mov ecx, KERNEL_SIZE
    rep movsb

    call vga_text_mode

    mov rax, [rip+img_base]
    mov edx, gdt - IMG_BASE
    add rdx, rax
    mov [rip+gdt_desc+2], edx
    lgdt [rip+gdt_desc]

    mov rax, [rip+img_base]
    lea rdx, [rip+.compat]
    sub rdx, IMG_BASE
    add rdx, rax
    mov dword ptr [rip+fj_buf], edx
    mov word ptr [rip+fj_buf+4], 0x08
    jmp far [rip+fj_buf]

.compat:
    mov rax, cr0
    and rax, 0x7FFFFFFF
    mov cr0, rax
    jmp far [rip+fj_buf]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    jmp KERN_LOAD

ebs_fail:
    lea rax, [rip+s_fail]
    mov rcx, [r14+0x40]
    mov rdx, rax
    mov rax, [rcx+8]
    sub rsp, 0x28
    call rax
    add rsp, 0x28
    mov dx, 0xF4
    mov al, 0x20
    out dx, al
1:
    pause
    jmp 1b

/* ---- GetMemoryMap with fresh size each attempt ---- */
get_mm:
    mov rax, 8192
    mov [rip+mm_size], rax
    mov rax, [r14+0x60]
    lea rcx, [rip+mm_size]
    lea rdx, [rip+mm_buf]
    lea r8, [rip+mm_key]
    lea r9, [rip+mm_dsize]
    lea r10, [rip+mm_dver]
    mov [rsp+0x20], r10
    mov rax, [rax+0x38]
    sub rsp, 0x28
    call rax
    add rsp, 0x28
    ret

/* ---- force VGA text mode 3 via register writes ---- */
vga_text_mode:
    mov dx, 0x3C2
    mov al, 0x67
    out dx, al

    lea rsi, [rip+seq_tab]
    mov ecx, seq_len
    mov dx, 0x3C4
1:
    mov al, [rsi]
    out dx, al
    inc dx
    mov al, [rsi+1]
    out dx, al
    dec dx
    add rsi, 2
    dec ecx
    jnz 1b

    mov dx, 0x3D4
    mov al, 0x11
    out dx, al
    inc dx
    in al, dx
    and al, 0x7F
    out dx, al
    dec dx

    lea rsi, [rip+crtc_tab]
    mov ecx, crtc_len
    xor ah, ah
2:
    mov al, ah
    out dx, al
    inc dx
    mov al, [rsi]
    out dx, al
    dec dx
    inc rsi
    inc ah
    dec ecx
    jnz 2b

    lea rsi, [rip+gc_tab]
    mov ecx, gc_len
    mov dx, 0x3CE
3:
    mov al, [rsi]
    out dx, al
    inc dx
    mov al, [rsi+1]
    out dx, al
    dec dx
    add rsi, 2
    dec ecx
    jnz 3b

    mov dx, 0x3DA
    in al, dx

    lea rsi, [rip+atc_tab]
    mov ecx, atc_len
    xor ah, ah
    mov dx, 0x3C0
4:
    mov al, ah
    out dx, al
    mov al, [rsi]
    out dx, al
    inc rsi
    inc ah
    dec ecx
    jnz 4b
    mov al, 0x20
    out dx, al
    ret

/* ---- data ---- */
    .balign 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_desc:
    .word 23
    .long 0

fj_buf:
    .long 0
    .word 0

img_base:
    .quad 0

mm_size:
    .quad 8192
mm_key:
    .quad 0
mm_dsize:
    .quad 0
mm_dver:
    .long 0

    .balign 8
mm_buf:
    .space 8192

g_li_guid:
    .byte 0xA1, 0x31, 0x1B, 0x5B, 0x62, 0x95, 0xD2, 0x11
    .byte 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B

s_start: .word 'G','R','I','D',' ','U','E','F','I',' ','l','o','a','d','e','r',0x0D,0x0A,0
s_image: .word 'h','a','n','d','o','f','f',0x0D,0x0A,0
s_fail:  .word 'f','a','i','l',0x0D,0x0A,0

seq_tab:
    .byte 0x00, 0x01
    .byte 0x01, 0x00
    .byte 0x02, 0x03
    .byte 0x03, 0x00
    .byte 0x04, 0x02
    .byte 0x00, 0x03
    .set seq_len, (. - seq_tab) / 2

crtc_tab:
    .byte 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F
    .byte 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50
    .byte 0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3
    .byte 0xFF
    .set crtc_len, . - crtc_tab

gc_tab:
    .byte 0x00, 0x00
    .byte 0x01, 0x00
    .byte 0x02, 0x00
    .byte 0x03, 0x00
    .byte 0x04, 0x00
    .byte 0x05, 0x10
    .byte 0x06, 0x0E
    .byte 0x07, 0x00
    .byte 0x08, 0xFF
    .set gc_len, (. - gc_tab) / 2

atc_tab:
    .byte 0x00, 0x01, 0x02, 0x03
    .byte 0x04, 0x05, 0x14, 0x07
    .byte 0x38, 0x39, 0x3A, 0x3B
    .byte 0x3C, 0x3D, 0x3E, 0x3F
    .byte 0x0C
    .byte 0x00
    .byte 0x0F
    .byte 0x00
    .byte 0x00
    .set atc_len, . - atc_tab

/* ---- embedded kernel ---- */
    .section .payload, "a"
    .globl payload_start
payload_start:
    .incbin "build/kernel.bin"
    .globl payload_end
payload_end:
