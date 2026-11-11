/* Native record cleanup removes only the suffix after a direct transfer.
   Labels and opaque asm remain possible entries; reachable effects survive. */
#define NI __attribute__((noinline))
static volatile int dc_seen;
NI void dc_effect(int x) { dc_seen += x; }
NI int dc_goto(int x)
{
    goto done;
    dc_effect(101);
    dc_seen = 303;
done:
    return x + 7;
}
NI int dc_return(int x)
{
    return x + 3;
    dc_effect(103);
    dc_seen = 305;
}
NI int dc_break(int n)
{
    while (n > 0) {
        ++n;
        break;
        dc_effect(107);
    }
    return n;
}
NI int dc_continue(int n)
{
    int sum = 0;
    while (n > 0) {
        sum += n--;
        continue;
        dc_effect(109);
    }
    return sum;
}
/* The taken and fallthrough paths must both reach their original labels. */
NI int dc_labels(int x)
{
    if (x == 1) goto first;
    if (x == 2) goto second;
    goto third;
    dc_effect(113);
first:
    dc_seen += 2;
    goto done;
    dc_effect(127);
second:
    dc_seen += 3;
    goto done;
third:
    dc_seen += 5;
done:
    return x + 11;
}
NI int dc_computed(int x)
{
    void *dest = x ? &&second : &&first;
    goto *dest;
first:
    dc_seen += 7;
    return 17;
second:
    dc_seen += 11;
    return 19;
}
static int dc_cleaned;
NI void dc_clean(int *p) { dc_cleaned += *p; }
NI int dc_cleanup(int x)
{
    {
        int value __attribute__((cleanup(dc_clean))) = x;
        if (x > 0) goto out;
        return x - 1;
    }
out:
    return x + 1;
}
NI int dc_vla(int n)
{
    int sum = 0;
    while (n > 0) {
        volatile int values[n];
        values[n - 1] = n;
        sum += values[n - 1];
        if (--n) continue;
        break;
        dc_effect(131);
    }
    return sum;
}
NI int dc_condition(int x)
{
    if (x < 0) dc_effect(13);
    else dc_effect(17);
    return x < 0 ? -x : x;
}
#if defined(__aarch64__)
/* Compile-only: a debugger may resume execution after BRK. */
NI void dc_builtin_debugtrap(void)
{
    __builtin_debugtrap();
    dc_effect(149);
}
NI void dc_debugtrap(void)
{
    __builtin_arm_trap(17);
    dc_effect(139);
}
#endif
#if defined(__x86_64__) || defined(__aarch64__)
/* A branch in raw asm is not a direct machine-record jump. The instructions
   after it still contain the asm fallthrough path and target entry. */
NI int dc_asm_goto(int x)
{
#if defined(__x86_64__)
    __asm__ goto("testl %0, %0; jnz %l[target]" : : "r"(x) : "cc" : target);
#else
    __asm__ goto("cbnz %w0, %l[target]" : : "r"(x) : : target);
#endif
    dc_seen += 19;
    return 23;
target:
    dc_seen += 23;
    return 29;
}
/* Retain the raw block itself even after an unconditional record jump.
   Directives and labels inside opaque text need not describe runtime code. */
NI int dc_raw_barrier(int x)
{
    goto done;
    __asm__ volatile("# dc_opaque_barrier" : : : "memory");
    dc_effect(137);
done:
    return x + 31;
}
#endif
int main(void)
{
    if (dc_goto(4) != 11 || dc_return(4) != 7 || dc_seen) return 1;
    if (dc_break(2) != 3 || dc_break(0) != 0 || dc_continue(4) != 10 || dc_seen) return 2;
    if (dc_labels(1) != 12 || dc_labels(2) != 13 || dc_labels(0) != 11 || dc_seen != 10) return 3;
    if (dc_computed(0) != 17 || dc_computed(1) != 19 || dc_seen != 28) return 4;
    if (dc_cleanup(4) != 5 || dc_cleanup(-3) != -4 || dc_cleaned != 1) return 5;
    if (dc_vla(5) != 15 || dc_seen != 28) return 6;
    if (dc_condition(-3) != 3 || dc_condition(4) != 4 || dc_seen != 58) return 7;
#if defined(__x86_64__) || defined(__aarch64__)
    if (dc_asm_goto(0) != 23 || dc_asm_goto(1) != 29 || dc_seen != 100) return 8;
    if (dc_raw_barrier(4) != 35 || dc_seen != 100) return 9;
#endif
    return 0;
}
