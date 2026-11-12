/* A builtin setjmp destination must preserve inherited callee-saves even
   without automatically promoted locals. Explicit register lifetimes make
   these values live for the reference compiler too. The intermediate frame
   jumps before restoring its asm clobbers, as a promoted frame can do in C. */
#if defined(__x86_64__)
static void *jump_buffer[5];
static volatile unsigned long before[5], after[5];
static volatile int visits;

__attribute__((noinline, used, noreturn)) void register_locals_jump(void)
{
    __builtin_longjmp(jump_buffer, 1);
    __builtin_unreachable();
}

__attribute__((noinline)) static void intermediate(void)
{
    __asm__ volatile("movq $11, %%rbx\n\tmovq $13, %%r13\n\t"
                     "movq $14, %%r14\n\tmovq $15, %%r15\n\t"
                     "movq $12, %%r12\n\t"
                     "call register_locals_jump"
                     : : : "rbx", "r13", "r14", "r15", "r12", "memory");
    __builtin_unreachable();
}

/* Reserve the captured registers so memory-operand address placement cannot
   overwrite them before the template reads their incoming values. */
#define CAPTURE(array) \
    __asm__ volatile("movq %%rbx, %0\n\tmovq %%r13, %1\n\t" \
                     "movq %%r14, %2\n\tmovq %%r15, %3\n\t" \
                     "movq %%r12, %4" \
                     : "=m"(array[0]), "=m"(array[1]), \
                       "=m"(array[2]), "=m"(array[3]), "=m"(array[4]) \
                     : : "rbx", "r12", "r13", "r14", "r15")

__attribute__((noinline)) static int destination(void)
{
    volatile int preserved = 123;
    register unsigned long a __asm__("rbx") = 101;
    register unsigned long b __asm__("r13") = 103;
    register unsigned long c __asm__("r14") = 107;
    register unsigned long d __asm__("r15") = 109;
    register unsigned long f __asm__("r12") = 113;
    __asm__ volatile("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(f));
    CAPTURE(before);
    if (!__builtin_setjmp(jump_buffer)) {
        ++visits;
        intermediate();
    }
    __asm__ volatile("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(f));
    CAPTURE(after);
    if (preserved != 123 || visits != 1) return 1;
    for (int i = 0; i < 5; ++i)
        if (before[i] != after[i]) return 2 + i;
    return 0;
}

/* This skipped frame establishes a different R12 alignment base. */
__attribute__((noinline)) static void aligned_intermediate(void)
{
    volatile unsigned long space[33] __attribute__((aligned(256)));
    space[0] = 19;
    space[32] = 23;
    if (space[0] + space[32] != 42) __builtin_trap();
    __builtin_longjmp(jump_buffer, 1);
    __builtin_unreachable();
}

/* The first aligned local is lowered only after the resume site. */
__attribute__((noinline)) static int aligned_destination(void)
{
    volatile int preserved = 47;
    if (!__builtin_setjmp(jump_buffer)) {
        ++visits;
        aligned_intermediate();
    }
    volatile unsigned long late[17] __attribute__((aligned(64)));
    late[0] = 11;
    late[16] = 31;
    if (((unsigned long)late & 63) || preserved != 47 || visits != 1)
        return 1;
    if (late[0] + late[16] != 42) return 2;
    return 0;
}

static inline __attribute__((always_inline)) int inline_aligned(int input)
{
    volatile int late __attribute__((aligned(128))) = input;
    if ((unsigned long)&late & 127) return -1;
    return late + 3;
}

/* Here alignment is introduced by an inlined body after setjmp. */
__attribute__((noinline)) static int inline_aligned_destination(void)
{
    volatile int preserved = 59;
    if (!__builtin_setjmp(jump_buffer)) {
        ++visits;
        aligned_intermediate();
    }
    if (inline_aligned(preserved) != 62 || visits != 1) return 1;
    return 0;
}

#endif

int main(void)
{
#if defined(__x86_64__)
    int result = destination();
    if (result) return result;
    visits = 0;
    result = aligned_destination();
    if (result) return 10 + result;
    visits = 0;
    result = inline_aligned_destination();
    return result ? 20 + result : 0;
#else
    return 0;
#endif
}
