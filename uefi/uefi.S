/*
 * GRID OS UEFI loader (v0.6) - hand-rolled PE32+ EFI application.
 *
 * No gnu-efi, no EDK2: the PE headers are written by hand below and the
 * image is linked as a flat binary. Layout (file offset == RVA):
 *
 *   0x0000  PE headers            (SizeOfHeaders = 0x200)
 *   0x1000  .text    loader code + data (8 KB reserved)
 *   0x3000  .payload kernel.bin embedded via .incbin
 *
 * The loader: prints progress on the UEFI console, kills the watchdog,
 * locates its own image via the LoadedImage protocol, captures the
 * memory map, calls ExitBootServices, copies the kernel to 0x8000,
 * forces VGA text mode 3 by register writes (GOP left the text buffer
 * unusable), then drops from long mode to 32-bit protected mode with
 * the exact GDT layout boot.S uses (CS 0x08 / DS 0x10) and jumps to
 * the kernel entry point.
 */

    .intel_syntax noprefix
    .include "build/uefi/sizes.inc"

    .set IMG_BASE,   0x100000
    .set TEXT_RVA,   0x1000
    .set PL_RVA,     0x4000
    .set KERN_LOAD,  0x8000
    .set SEQ_LEN,    6
    .set CRTC_LEN,   25
    .set GC_LEN,     9
    .set ATC_LEN,    21
    .set FJ_ADDR,    0x4F0

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
    .long 0, 0
    .word 0, 0
    .long 0x60000020
sec_payload:
    .ascii ".payload"
    .long KERNEL_SIZE
    .long PL_RVA
    .long PAYLOAD_RAW
    .long PL_RVA
    .long 0, 0
    .word 0, 0
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
    sub rsp, 8

    lea rax, [rip+s_start]
    call print

    mov rax, [r14+0x60]
    mov rcx, 0
    mov rdx, 0
    mov r8, 0
    mov r9, 0
    mov rax, [rax+0x100]
    sub rsp, 0x28
    call rax
    add rsp, 0x28

    mov rax, [r14+0x60]
    mov rcx, r15
    lea rdx, [rip+g_li_guid]
    lea r8, [rip+img_base]
    mov rax, [rax+0x98]
    sub rsp, 0x28
    call rax
    add rsp, 0x28
    test eax, eax
    jnz ebs_fail

    lea rax, [rip+s_image]
    call print

    lea rax, [rip+s_ebs]
    call print

    mov ebx, 3
ebs_loop:
    call get_mm
    test eax, eax
    jnz ebs_fail

    mov rax, [r14+0x60]
    mov rcx, r15
    mov rdx, [rip+mm_key]
    mov rax, [rax+0xE8]
    sub rsp, 0x28
    call rax
    add rsp, 0x28
    test eax, eax
    jz ebs_ok
    dec ebx
    jnz ebs_loop
    jmp ebs_fail

ebs_ok:
    call make_e820

    mov rsi, [rip+img_base]
    add rsi, PL_RVA
    mov edi, KERN_LOAD
    mov ecx, KERNEL_SIZE
    rep movsb
    mov al, 'C'
    call ser_byte

    call vga_text_mode
    mov al, 'V'
    call ser_byte

    mov rax, [rip+img_base]
    lea rdx, [rip+gdt]
    sub rdx, IMG_BASE
    add rdx, rax
    mov [rip+gdt_desc+2], rdx
    lgdt [rip+gdt_desc]
    mov al, 'G'
    call ser_byte

    mov rax, [rip+img_base]
    lea rdx, [rip+.compat]
    sub rdx, IMG_BASE
    add rdx, rax
    mov [FJ_ADDR], edx
    mov word ptr [FJ_ADDR+4], 0x08
    .byte 0xFF, 0x2C, 0x25
    .long FJ_ADDR

.compat:
    mov al, 'M'
    call ser_byte
    mov rax, cr0
    and rax, 0x7FFFFFFF
    mov cr0, rax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov al, 'K'
    call ser_byte
    jmp KERN_LOAD

ebs_fail:
    mov esi, eax
    lea rdi, [rip+fail_buf]
    mov ecx, 8
1:
    rol esi, 4
    mov eax, esi
    and eax, 0xF
    cmp eax, 10
    jb 2f
    add eax, 'A'-10
    jmp 3f
2:
    add eax, '0'
3:
    mov word [rdi], ax
    add rdi, 2
    dec ecx
    jnz 1b
    mov word ptr [rdi], 0x0A0D
    mov word ptr [rdi+2], 0
    lea rax, [rip+s_fail]
    call print
    lea rax, [rip+fail_buf]
    call print
    mov dx, 0xF4
    mov al, 0x20
    out dx, al
1:
    pause
    jmp 1b

/* ---- ConOut OutputString(rax = UTF-16 string) ---- */
print:
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 0x20
    mov rcx, [r14+0x40]
    mov rdx, rax
    mov rax, [rcx+8]
    call rax
    mov rsp, rbp
    pop rbp
    ret

/* ---- GetMemoryMap with fresh size each attempt ---- */
get_mm:
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 0x30
    mov rax, 8192
    mov [rip+mm_size], rax
    mov rax, [r14+0x60]
    lea rcx, [rip+mm_size]
    lea rdx, [rip+mm_buf]
    lea r8, [rip+mm_key]
    lea r9, [rip+mm_dsize]
    lea r10, [rip+mm_dver]
    mov rax, [rax+0x38]
    mov [rsp+0x20], r10
    call rax
    mov rsp, rbp
    pop rbp
    ret

/* ---- convert UEFI memory map (mm_buf) to e820: count u16 @0x4FE,
 *      24-byte entries (base u64, len u64, type u32, pad u32) @0x500 ---- */
make_e820:
    mov word ptr [0x4FE], 0
    lea rsi, [rip+mm_buf]
    mov r13d, 24
    mov edi, 0x500
.next:
    test r13d, r13d
    jz .done
    mov r10, [rip+mm_dsize]
    test r10, r10
    jz .done
    mov r8d, [rsi]
    mov rax, [rsi+8]
    mov rcx, [rsi+24]
    shl rcx, 12
    jrcxz .skip
    mov edx, 2
    cmp r8d, 7
    je .usable
    cmp r8d, 3
    je .usable
    cmp r8d, 4
    je .usable
    cmp r8d, 1
    je .usable
    cmp r8d, 2
    je .usable
    cmp r8d, 9
    jne .nvs
    mov edx, 3
    jmp .store
.nvs:
    cmp r8d, 10
    jne .store
    mov edx, 4
    jmp .store
.usable:
    mov edx, 1
.store:
    mov [rdi], rax
    mov [rdi+8], rcx
    mov [rdi+16], edx
    mov dword ptr [rdi+20], 0
    add rdi, 24
    inc word ptr [0x4FE]
    dec r13d
.skip:
    add rsi, r10
    jmp .next
.done:
    ret

/* ---- raw COM1 byte in al (works before and after ExitBootServices) ---- */
ser_byte:
    mov ah, al
    mov dx, 0x3FD
1:
    in al, dx
    test al, 0x20
    jz 1b
    mov al, ah
    mov dx, 0x3F8
    out dx, al
    ret

/* ---- force VGA text mode 3 via register writes ---- */
vga_text_mode:
    mov dx, 0x3C2
    mov al, 0x67
    out dx, al

    lea rsi, [rip+seq_tab]
    mov ecx, SEQ_LEN
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
    mov ecx, CRTC_LEN
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
    mov ecx, GC_LEN
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
    mov ecx, ATC_LEN
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
    .quad 0

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
s_image: .word 'h','p',0x0D,0x0A,0
s_ebs:   .word 'e','b','s',0x0D,0x0A,0
s_fail:  .word 'f','a','i','l',0x0D,0x0A,0

fail_buf:
    .space 24

seq_tab:
    .byte 0x00, 0x01
    .byte 0x01, 0x00
    .byte 0x02, 0x03
    .byte 0x03, 0x00
    .byte 0x04, 0x02
    .byte 0x00, 0x03

crtc_tab:
    .byte 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F
    .byte 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50
    .byte 0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3
    .byte 0xFF

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

/* ---- embedded kernel ---- */
    .section .payload, "a"
    .globl payload_start
payload_start:
    .incbin "build/kernel.bin"
    .globl payload_end
payload_end:
