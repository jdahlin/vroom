/* Pure constant control proofs must neither evaluate dead operands nor erase
 * calls, volatile/atomic reads, or truncation before the truth decision. */
#define NI __attribute__((noinline))
static NI int never_control(unsigned x) { return x + 99; }
/* A libc-free implementation for the baseline O0 builtin calls. */
NI int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
static volatile unsigned observed;
static unsigned atom;
static unsigned calls;
static NI unsigned step(void) { return ++calls; }

NI unsigned control_parameter(unsigned x)
{
    if (__builtin_constant_p(x) && never_control(x)) return 99;
    if (!__builtin_constant_p(x) || never_control(x+1)) return x+1;
    return 0;
}

NI unsigned control_strings(void)
{
    if (!__builtin_strcmp("a", "a")) {
        if (__builtin_strcmp("a", "b")) return 17;
        else return never_control(1);
    } else return never_control(2);
}

NI unsigned control_strings_value(void)
{
    return __builtin_strcmp("a", "a") != 0 || __builtin_strcmp("a", "b") >= 0;
}

NI unsigned control_named_call(void)
{
    return strcmp("same", "same");
}

NI unsigned control_calls(unsigned x)
{
    unsigned before = calls;
    if (step() && __builtin_constant_p(x)) return 99;
    if (step() || !__builtin_constant_p(x)) { }
    return calls - before;
}

NI unsigned control_not_evaluated(void)
{
    unsigned before = calls;
    unsigned a = (_Bool)__builtin_constant_p(step());
    unsigned b = 0 && step();
    unsigned c = 1 || step();
    return 100*(calls-before) + a+b+c;
}

NI unsigned control_volatile(void)
{
    return observed && 0;
}

NI unsigned control_atomic(void)
{
    return __atomic_load_n(&atom, __ATOMIC_RELAXED) && 0;
}

NI unsigned control_casts(void)
{
    if ((unsigned char)256) return never_control(3);
    else if (!(unsigned char)256) return 19;
    else return never_control(4);
}

int main(void)
{
    observed = 7;
    atom = 9;
    for (unsigned i=0;i<16;i++) {
        if (control_parameter(i) != i+1 || control_strings() != 17) return 1;
        if (control_strings_value() || control_named_call() || control_calls(i) != 2) return 2;
        if (__builtin_strcmp("\200", "\177") <= 0 || __builtin_strcmp("a\0b", "a\0c")) return 5;
        if (control_not_evaluated() != 1 || control_casts() != 19) return 3;
        if (control_volatile() || control_atomic()) return 4;
    }
    return 0;
}
