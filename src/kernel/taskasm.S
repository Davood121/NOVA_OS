# GRID OS v0.9 - task resume (noreturn).
# Called with interrupts disabled from the PIT tick path (task_tick)
# or from a finishing task's trampoline. Loads the target task's saved
# kernel register frame and iretd's into it. The caller's context is
# abandoned; it was already saved (or deliberately not, if DONE).
#
# Frame layout (from esp upward, built by isr.S or task_spawn):
#   gs, fs, es, ds, edi, esi, ebp, esp, ebx, edx, ecx, eax,
#   int_no, err_code, eip, cs, eflags

    .intel_syntax noprefix
    .code32

.globl task_resume
task_resume:
    mov eax, [esp+4]     # kesp of the task to resume
    mov esp, eax         # abandon the caller's stack entirely
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8           # skip int_no + err_code
    iretd
