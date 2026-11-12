    .intel_syntax noprefix
    .code32
    .globl _start
    .extern kmain
    .extern __bss_start
    .extern __bss_end

_start:
    mov esp, 0x74000

    mov eax, offset __bss_start
    mov ebx, offset __bss_end
1:
    cmp eax, ebx
    jae 2f
    mov byte ptr [eax], 0
    inc eax
    jmp 1b
2:
    call kmain
3:
    hlt
    jmp 3b
