/* Shifts by a count in a register under BMI2 (shlx, shrx, sarx): both widths,
 * counts reused and aliased with their operands, many live values; results
 * the same as clang's.  The shifting runs only on a CPU with BMI2. */
int printf(const char *, ...);
#ifdef __x86_64__
__attribute__((noinline, target("bmi2"))) static unsigned long long
mix(const unsigned long long *v, int rounds)
{
    unsigned long long a = v[0], b = v[1], c = v[2], d = v[3], h = 0;
    unsigned e = (unsigned)v[4], f = (unsigned)v[5];
    long long s = (long long)v[6];

    for (int i = 0; i < rounds; i++) {
        int n = (int)(a & 63), m = (int)(b & 31);

        a = a << (c & 63) | b >> n;
        b = b >> (d & 63) ^ c << n;
        s = s >> m ^ (long long)a;
        c += (unsigned long long)(s >> (e & 63));
        e = e << m | f >> (f & 31);
        f = (unsigned)((int)f >> (e & 31)) + (e << (m & 31));
        d ^= 1ull << (a & 63);
        d += d << (d & 63);
        h = h * 31 + a + b + c + d + e + f + (unsigned long long)s;
    }
    return h;
}
int main(void)
{
    unsigned long long v[7] = { 0x9e3779b97f4a7c15ull, 0xbf58476d1ce4e5b9ull,
                                0x94d049bb133111ebull, 0x2545f4914f6cdd1dull,
                                0x1234567u, 0x89abcdefu, 0xfedcba9876543210ull };
    unsigned a, b, c, d;

    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    if (b >> 8 & 1)
        for (int k = 0; k < 40; k += 3)
            printf("%016llx\n", mix(v, k));
    return 0;
}
#else
int main(void)
{
    printf("not x86-64\n");
    return 0;
}
#endif
