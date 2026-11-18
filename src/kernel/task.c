/* GRID OS v0.9 - preemptive round-robin task scheduler (ring 0).
 *
 * Tasks are kernel threads with private stacks (4 KB from kmalloc).
 * The context switch happens on the PIT tick inside the IRQ frame:
 * task_tick() records the interrupted task's saved register frame,
 * picks the next task round-robin, and resumes it via task_resume()
 * (taskasm.S), which reloads the saved frame and iretd's into it.
 *
 * Task 0 ("main") is the boot context (kmain/shell) and keeps using
 * the original boot stack until its first tick saves the frame.
 * Finished tasks are marked DONE and never scheduled again.
 */
#include "kernel.h"

#define MAX_TASKS 8
#define SLICE_TICKS 2
#define TASK_STACK 4096
#define TASK_NAME_MAX 12

typedef enum { T_UNUSED = 0, T_READY, T_RUNNING, T_DONE } tstate_t;

struct task {
    uint32_t kesp;      /* saved kernel-stack frame (struct layout in idt.c) */
    tstate_t state;
    int pid;
    char name[TASK_NAME_MAX];
    void (*entry)(void);
    uint8_t *stack;     /* kmalloc'd base, 0 for the boot task */
    volatile uint32_t counter;
};

static struct task tasks[MAX_TASKS];
static volatile int cur = -1;    /* index of the running task */
static volatile int sched_on = 0;
static int slice = 0;
static int next_pid = 1;

/* taskasm.S - restores the saved register frame at kesp and iretd's
 * into the task. Never returns; caller's context is abandoned. */
void task_resume(uint32_t kesp) __attribute__((noreturn));

static void name_copy(char *dst, const char *src) {
    int i;
    for (i = 0; i + 1 < TASK_NAME_MAX && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

void sched_init(void) {
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = T_UNUSED;
        tasks[i].kesp = 0;
        tasks[i].stack = 0;
        tasks[i].entry = 0;
        tasks[i].counter = 0;
        tasks[i].pid = -1;
        tasks[i].name[0] = 0;
    }
    /* task 0: the boot context itself */
    tasks[0].state = T_RUNNING;
    tasks[0].pid = 0;
    name_copy(tasks[0].name, "main");
    cur = 0;
    slice = 0;
    sched_on = 1;
}

/* round-robin pick starting after 'from'; returns 'from' if nobody else */
static int pick_next(int from) {
    int i, n;
    for (n = 1; n < MAX_TASKS; n++) {
        i = (from + n) % MAX_TASKS;
        if (tasks[i].state == T_READY) return i;
    }
    return from;
}

/* called from isr_dispatch after the timer handler, still inside the
 * IRQ frame; may switch away (task_resume never returns back here) */
void task_tick(void *regs) {
    int from, n;
    if (!sched_on || cur < 0) return;
    tasks[cur].kesp = (uint32_t)regs;
    if (++slice < SLICE_TICKS) return;
    slice = 0;
    n = pick_next(cur);
    if (n == cur) return;
    from = cur;
    tasks[from].state = T_READY;
    tasks[n].state = T_RUNNING;
    cur = n;
    task_resume(tasks[n].kesp);
}

/* spawned tasks land here after the fake initial frame resumes;
 * after entry returns the task is retired and the CPU switches away */
static void task_trampoline(void) {
    int self = cur;
    tasks[self].entry();
    __asm__ volatile("cli");
    tasks[self].state = T_DONE;
    {
        int n = pick_next(self);
        if (n == self) {
            /* nothing else runnable: park forever */
            for (;;) __asm__ volatile("cli; hlt");
        }
        cur = n;
        tasks[n].state = T_RUNNING;
        task_resume(tasks[n].kesp);
    }
}

int task_spawn(const char *name, void (*entry)(void)) {
    int i;
    uint8_t *stk;
    uint32_t *sp;
    for (i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == T_UNUSED) break;
    if (i == MAX_TASKS) return -1;
    stk = kmalloc(TASK_STACK);
    if (!stk) return -1;
    tasks[i].stack = stk;
    tasks[i].entry = entry;
    tasks[i].counter = 0;
    tasks[i].pid = next_pid++;
    name_copy(tasks[i].name, name);
    /* build an initial IRQ-style register frame on the new stack so the
     * first resume goes straight into task_trampoline */
    sp = (uint32_t *)(stk + TASK_STACK - 19 * 4);
    sp[0] = 0x10;               /* gs */
    sp[1] = 0x10;               /* fs */
    sp[2] = 0x10;               /* es */
    sp[3] = 0x10;               /* ds */
    sp[4] = 0; sp[5] = 0;       /* edi esi */
    sp[6] = 0; sp[7] = 0;       /* ebp esp_dummy */
    sp[8] = 0; sp[9] = 0;       /* ebx edx */
    sp[10] = 0; sp[11] = 0;     /* ecx eax */
    sp[12] = 0;                 /* int_no */
    sp[13] = 0;                 /* err_code */
    sp[14] = (uint32_t)task_trampoline; /* eip */
    sp[15] = 0x08;              /* cs */
    sp[16] = 0x202;             /* eflags: IF=1 */
    tasks[i].kesp = (uint32_t)sp;
    tasks[i].state = T_READY;
    return tasks[i].pid;
}

int task_alive_count(void) {
    int i, c = 0;
    for (i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != T_UNUSED && tasks[i].state != T_DONE) c++;
    return c;
}

int task_current_pid(void) {
    return (cur >= 0) ? tasks[cur].pid : -1;
}

uint32_t task_counter(int pid) {
    int i;
    for (i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != T_UNUSED && tasks[i].pid == pid)
            return tasks[i].counter;
    return 0;
}

void task_print_list(void) {
    int i;
    static const char *stname[4] = { "unused", "ready", "running", "done" };
    for (i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == T_UNUSED) continue;
        console_puts("  ");
        console_putdec((uint32_t)tasks[i].pid);
        console_puts("  ");
        console_puts(tasks[i].name);
        console_puts("  ");
        console_puts(stname[tasks[i].state]);
        console_puts("  ticks=");
        console_putdec(tasks[i].counter);
        console_putc('\n');
    }
}
