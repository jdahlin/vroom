/* builtins with a C body (builtin-body.cpp): clang's carry chains.  On
   x86-64 the body is what the code generators see; arm64 lowers them itself.
   Each argument is evaluated once, in order; results are clang's. */
#define CHECK(cond, code) do { if (!(cond)) return code; } while (0)

static int trace_n;
static unsigned long long trace[8];
static unsigned long long note(unsigned long long v)
{
    trace[trace_n++] = v;
    return v;
}

int main(void)
{
    unsigned c;
    unsigned long cl;
    unsigned long long cll;
    unsigned short cs;
    unsigned char cb;

    CHECK(__builtin_addc(0xffffffffu, 1u, 0u, &c) == 0 && c == 1, 1);
    CHECK(__builtin_addc(0xffffffffu, 0xffffffffu, 1u, &c) == 0xffffffffu && c == 1, 2);
    CHECK(__builtin_addc(1u, 2u, 1u, &c) == 4 && c == 0, 3);
    CHECK(__builtin_addc(0u, 0u, 0u, &c) == 0 && c == 0, 4);
    CHECK(__builtin_subc(0u, 1u, 0u, &c) == 0xffffffffu && c == 1, 5);
    CHECK(__builtin_subc(5u, 3u, 1u, &c) == 1 && c == 0, 6);
    CHECK(__builtin_subc(0u, 0u, 1u, &c) == 0xffffffffu && c == 1, 7);
    CHECK(__builtin_subc(0u, 0xffffffffu, 1u, &c) == 0 && c == 1, 8);
    CHECK(__builtin_addcl(~0ul, 1ul, 0ul, &cl) == 0 && cl == 1, 9);
    CHECK(__builtin_subcl(0ul, 1ul, 1ul, &cl) == ~0ul - 1 && cl == 1, 10);
    CHECK(__builtin_addcll(~0ull, ~0ull, 1ull, &cll) == ~0ull && cll == 1, 11);
    CHECK(__builtin_subcll(0ull, 0ull, 1ull, &cll) == ~0ull && cll == 1, 12);
    CHECK(__builtin_addcb(255, 1, 0, &cb) == 0 && cb == 1, 13);
    CHECK(__builtin_addcb(200, 55, 1, &cb) == 0 && cb == 1, 14);
    CHECK(__builtin_subcb(0, 1, 0, &cb) == 255 && cb == 1, 15);
    CHECK(__builtin_addcs(0xffff, 0xffff, 1, &cs) == 0xffff && cs == 1, 16);
    CHECK(__builtin_subcs(0, 1, 0, &cs) == 0xffff && cs == 1, 17);

    /* a carry-in larger than 1 is added as it is */
    CHECK(__builtin_addc(0xfffffffeu, 0u, 3u, &c) == 1 && c == 1, 18);

    /* the carry out is also the carry in: read before the store */
    c = 1;
    CHECK(__builtin_addc(0xffffffffu, 0u, c, &c) == 0 && c == 1, 19);
    c = 0;
    CHECK(__builtin_subc(0u, 0u, c, &c) == 0 && c == 0, 20);

    /* a 256-bit addition by four limbs */
    {
        unsigned long long a[4] = {~0ull, ~0ull, ~0ull, 0}, b[4] = {1, 0, 0, 0}, r[4], k = 0;
        for (int i = 0; i < 4; i++)
            r[i] = __builtin_addcll(a[i], b[i], k, &k);
        CHECK(r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 1 && k == 0, 21);
    }

    /* each argument once, in order; arguments narrower than the type are converted */
    trace_n = 0;
    CHECK(__builtin_addcll(note(1), note(2), note(3), (note(4), &cll)) == 6 && cll == 0, 22);
    CHECK(trace_n == 4 && trace[0] == 1 && trace[1] == 2 && trace[2] == 3 && trace[3] == 4, 23);
    {
        volatile unsigned char small = 255;
        int n = 0;
        unsigned *p = &c;
        CHECK(__builtin_addc(small, (unsigned char)1, n++, p + n - 1) == 256 && c == 0 && n == 1, 24);
    }
    return 0;
}
