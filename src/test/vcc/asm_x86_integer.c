#if defined(__x86_64__)
static int once;
static int value(void) { ++once; return 7; }
static int basic(int x)
{
    int out;
    __asm__("addl %2, %0" : "=r"(out) : "0"(x), "r"(value()) : "cc");
    return out;
}
static int memory(int *p)
{
    int value;
    __asm__ volatile("addl $5, %[dst]\n\tmovl %[dst], %[out]"
                     : [dst] "+m"(*p), [out] "=&r"(value) : : "cc", "memory");
    return value;
}
static int fixed(int x)
{
    int y;
    __asm__("movl %%eax, %%ebx\n\taddl $9, %%ebx\n\tmovl %%ebx, %0"
            : "=d"(y) : "a"(x) : "ebx", "cc");
    return y;
}
static int branch(int x)
{
    int out = 3;
    __asm__ goto("testl %1, %1; jz %l[zero]; addl $5, %0; jmp %l[nonzero]"
                 : "+r"(out) : "r"(x) : "cc" : zero, nonzero);
    return -1;
zero:
    return out;
nonzero:
    return out;
}
static int numeric_branch(int x)
{
    int out = 4;
    __asm__ goto("testl %1, %1; jz %l3; addl $7, %0"
                 : "+r"(out) : "r"(x) : "cc" : zero);
    return out;
zero:
    return out + 1;
}
static int aligned_asm(void)
{
    volatile int values[8] __attribute__((aligned(64))) = { 3 };
    int out;
    __asm__("movl %1, %0" : "=r"(out) : "m"(values[0]));
    return out;
}
int main(void)
{
    if (branch(0) != 3 || branch(1) != 8) return 12;
    if (numeric_branch(0) != 5 || numeric_branch(1) != 11) return 13;
    if (aligned_asm() != 3) return 14;
    int alt, input = 19;
    __asm__("movl %1, %0; addl $2, %0" : "=&a,&r"(alt) : "a,r"(input) : "cc");
    if (alt != 21) return 16;
    __asm__("movl %1, %0" : "=r,r"(alt) : "m,r"(8));
    if (alt != 8) return 17;
    __asm__("addl $3, %1" : "+r"(alt) : : "cc");
    if (alt != 11) return 18;
    int a = 11, b = 21;
    unsigned long long q = 0x123456789abcdef0ULL;
    unsigned x;
    if (basic(5) != 12 || once != 1) return 1;
    if (memory(&a) != 16 || a != 16) return 2;
    if (fixed(12) != 21) return 3;
    __asm__("addl %1, %0" : "+r"(b) : "i"(4) : "cc");
    if (b != 25) return 4;
    __asm__("movl %k1, %0" : "=r"(x) : "r"(q));
    if (x != 0x9abcdef0U) return 5;
    __asm__("addl $%c1, %0" : "+r"(x) : "n"(3) : "cc");
    if (x != 0x9abcdef3U) return 6;
    __asm__ volatile("jmp .Ldone%=\n.Ldone%=:\n" : : : "memory");
    __asm__ volatile("nop");
    register unsigned long long forced __asm__("r12") = 37;
    __asm__("addq $2, %%r12" : "+r"(forced) : : "cc");
    if (forced != 39) return 7;
    float f = 1.0f;
    __asm__("addl $0x800000, %0" : "+r"(f) : : "cc");
    if (f != 2.0f) return 8;
    unsigned bits;
    __asm__("movl %1, %0" : "=r"(bits) : "r"(f));
    if (bits != 0x40000000U) return 9;
    struct { unsigned a, b; } pair = { 0x12345678U, 0x9abcdef0U };
    __asm__("rolq $32, %0" : "+r"(pair) : : "cc");
    if (pair.a != 0x9abcdef0U || pair.b != 0x12345678U) return 10;
    unsigned long long packed;
    __asm__("movq %1, %0" : "=r"(packed) : "r"(pair));
    if (packed != 0x123456789abcdef0ULL) return 11;
    return 0;
}
#else
int main(void) { return 0; }
#endif
