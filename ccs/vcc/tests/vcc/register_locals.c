/* Mutable scalar locals must survive calls, narrowing and memory barriers. */
#define NI __attribute__((noinline))
static volatile int rl_seen;
NI int rl_step(int x) { rl_seen++; return x * 3 + 1; }
NI int rl_cold(int n) { return n + 1; }
NI int rl_loop(int n)
{
    int sum = 7;
    for (int i = 0; i < n; i++) sum += i * 3 + 1;
    return sum;
}
NI int rl_calls(int n)
{
    int sum = 7;
    for (int i = 0; i < n; i++) sum += rl_step(i);
    return sum;
}
NI long long rl_params(int a, int b, int c, int d, int e, int f,
                       int g, int h, int i, int j)
{
    a += j; g -= b; h += c; i -= d;
    return (long long)a + e + f + g + h + i;
}
NI int rl_narrow(int n)
{
    signed char s = 126;
    unsigned char u = 254;
    short t = -32000;
    unsigned short v = 65530;
    for (int i = 0; i < n; i++) { s += 3; u += 3; t += 127; v += 7; }
    return s + u + t + v;
}
NI int rl_mutate(int *p) { *p += 19; return *p; }
NI int rl_escaped(int n) { int x = n; int y = rl_mutate(&x); return x + y; }
NI int rl_pointer(void)
{
    int a[4] = {3, 5, 7, 11};
    int *p = a;
    int sum = 0;
    for (int i = 0; i < 4; i++) sum += *p++;
    return sum + (p - a);
}
NI int rl_volatile(int n)
{
    volatile int x = n;
    for (int i = 0; i < 3; i++) x += i + 1;
    return x;
}
NI int rl_atomic(int n) { _Atomic int x = n; x = n + 3; return x; }
static void rl_clean(int *p) { rl_seen += *p; }
NI int rl_cleanup(int n)
{
    int x __attribute__((cleanup(rl_clean))) = n;
    for (int i = 0; i < 3; i++) x += i;
    return x;
}
static inline __attribute__((always_inline)) int rl_inline(int x)
{
    int y = x;
    for (int i = 0; i < 3; i++) y += i + x;
    return y;
}
NI int rl_instances(int n) { return rl_inline(n) + rl_inline(n + 2); }
NI int rl_aligned(int n)
{
    int x __attribute__((aligned(64))) = n;
    int y = rl_mutate(&x);
    return x + y + (((__UINTPTR_TYPE__)&x & 63) != 0);
}
static inline __attribute__((always_inline)) int rl_inline_aligned(int n)
{
    int x __attribute__((aligned(64))) = n;
    int y = rl_mutate(&x);
    return x + y + (((__UINTPTR_TYPE__)&x & 63) != 0);
}
NI int rl_nested_aligned(int n)
{
    int a = n + 3;
    int b = rl_inline_aligned(n);
    return a + b;
}
NI int rl_vla(int n)
{
    volatile int a[n];
    int sum = 0;
    for (int i = 0; i < n; i++) { a[i] = i + 1; sum += a[i]; }
    return sum;
}
NI int rl_varargs(int count, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, count);
    int sum = 0;
    for (int i = 0; i < count; i++) sum += __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return sum;
}
#if defined(__x86_64__)
static inline __attribute__((always_inline)) long rl_inline_asm(long n)
{
    long x = n + 1;
    __asm__ volatile("add $1, %0" : "+b"(x));
    return x;
}
NI long rl_nested_asm(long n)
{
    long a = n + 3, b = n + 7;
    long x = rl_inline_asm(n);
    return a + b + x;
}
NI int rl_asm(int n)
{
    int x = n + 3;
    __asm__ volatile("mov $123, %%rbx; mov $456, %%r13; mov $789, %%r14; mov $321, %%r15"
                     : "+m"(x) : : "rbx", "r13", "r14", "r15", "memory");
    return x + 7;
}
NI long rl_fixed(long n)
{
    register long x __asm__("r13") = n + 3;
    __asm__ volatile("add $1, %0" : "+r"(x));
    return x + 7;
}
#elif defined(__aarch64__)
NI int rl_asm(int n)
{
    int x = n + 3;
    __asm__ volatile("mov x19, #123; mov x20, #456; mov x21, #789; mov x22, #321"
                     : "+m"(x) : : "x19", "x20", "x21", "x22", "memory");
    return x + 7;
}
NI long rl_fixed(long n)
{
    register long x __asm__("x19") = n + 3;
    __asm__ volatile("add %0, %0, #1" : "+r"(x));
    return x + 7;
}
#endif
/* All four independent homes remain live across opaque clobbers. The
   repeated scalar accesses give these homes priority over the parameters. */
NI long rl_asm_four(long n)
{
    long a = n + 1, b = n + 2, c = n + 3, d = n + 4;
    a += 10; b += 20; c += 30; d += 40;
    a += 1; a -= 1; a += 1; a -= 1;
    b += 1; b -= 1; b += 1; b -= 1;
    c += 1; c -= 1; c += 1; c -= 1;
    d += 1; d -= 1; d += 1; d -= 1;
#if defined(__x86_64__)
    __asm__ volatile("mov $123, %%rbx; mov $456, %%r13; mov $789, %%r14; mov $321, %%r15"
                     : : : "rbx", "r13", "r14", "r15");
#elif defined(__aarch64__)
    __asm__ volatile("mov x19, #123; mov x20, #456; mov x21, #789; mov x22, #321"
                     : : : "x19", "x20", "x21", "x22");
#endif
    return a + 2 * b + 3 * c + 4 * d;
}
NI long rl_asm_goto_four(long n, int take)
{
    long a = n + 1, b = n + 2, c = n + 3, d = n + 4;
    a += 10; b += 20; c += 30; d += 40;
    a += 1; a -= 1; a += 1; a -= 1;
    b += 1; b -= 1; b += 1; b -= 1;
    c += 1; c -= 1; c += 1; c -= 1;
    d += 1; d -= 1; d += 1; d -= 1;
#if defined(__x86_64__)
    __asm__ goto("mov $123, %%rbx; mov $456, %%r13; mov $789, %%r14; mov $321, %%r15; "
                 "test %0, %0; jne %l1"
                 : : "r"(take) : "rbx", "r13", "r14", "r15" : taken);
#elif defined(__aarch64__)
    __asm__ goto("mov x19, #123; mov x20, #456; mov x21, #789; mov x22, #321; "
                 "cbnz %w0, %l1"
                 : : "r"(take) : "x19", "x20", "x21", "x22" : taken);
#else
    if (take) goto taken;
#endif
    return a + 2 * b + 3 * c + 4 * d;
taken:
    return a + 2 * b + 3 * c + 4 * d + 5;
}
int main(void)
{
    if (rl_cold(5) != 6) return 17;
    if (rl_loop(8) != 99) return 1;
    rl_seen = 0;
    if (rl_calls(8) != 99 || rl_seen != 8) return 2;
    if (rl_params(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 43) return 3;
    if (rl_narrow(5) != -31438) return 4;
    if (rl_escaped(5) != 48) return 5;
    if (rl_pointer() != 30) return 12;
    if (rl_volatile(5) != 11 || rl_atomic(5) != 8) return 6;
    rl_seen = 0;
    if (rl_cleanup(5) != 8 || rl_seen != 8) return 7;
    if (rl_instances(5) != 54) return 8;
    if (rl_aligned(5) != 48 || rl_vla(7) != 28) return 9;
    if (rl_nested_aligned(5) != 56) return 14;
    if (rl_varargs(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55) return 10;
#if defined(__x86_64__) || defined(__aarch64__)
    if (rl_asm(5) != 15 || rl_fixed(5) != 16) return 11;
#endif
#if defined(__x86_64__)
    if (rl_nested_asm(5) != 27) return 13;
#endif
    if (rl_asm_four(5) != 380) return 15;
    if (rl_asm_goto_four(5, 0) != 380 || rl_asm_goto_four(5, 1) != 385) return 16;
    return 0;
}
