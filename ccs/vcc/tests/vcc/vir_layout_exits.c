/* Block layout and exits (VIR at -O2): returns that end in their own epilogue,
   blocks that only jump on copied to the end of the blocks that jump to them, a function that calls nothing keeping its slots and
   callee-saved registers below sp without a frame record (sub sp ... add sp),
   also before a sibling call, and the frame without mov sp, x29. */
#define NI __attribute__((noinline))
typedef unsigned long long U;

static volatile int sink;
NI int id(int x) { return x + sink; }

/* many returns, each with a value, from a function with a frame */
NI int classify(int x, int y)
{
    if (x < 0)
        return id(-1);
    if (x == 0)
        return 0;
    if (x < 10)
        return y ? 1 : 2;
    if (x < 100)
        return id(3) + y;
    return 4;
}

/* a loop with a small test block */
NI int count(const int *p, int n, int k)
{
    int c = 0, i = 0;

    while (i < n) {
        if (p[i] == k)
            c++;
        i++;
    }
    return c;
}

/* a loop with continue and break and a small join */
NI int scan(const unsigned char *s)
{
    int n = 0;

    for (;; s++) {
        if (*s == ' ')
            continue;
        if (!*s)
            break;
        n += *s & 1 ? 3 : 1;
    }
    return n;
}

/* a leaf with a local array and many live values: callee-saved registers and
   slots, no call, no frame record */
NI U leafy(U a, U b, U c, U d, const U *p)
{
    volatile U r[6];
    U s = 0, x0 = a * 3, x1 = b * 5, x2 = c * 7, x3 = d * 11, x4 = a ^ b, x5 = c ^ d, x6 = a + d, x7 = b + c,
      x8 = a - c, x9 = b - d, x10 = a | d, x11 = b & c, x12 = a * b, x13 = c * d, x14 = a + b + c;

    for (int i = 0; i < 6; i++)
        r[i] = p[i] + x0 * i;
    for (int i = 0; i < 6; i++)
        s += r[i] ^ x1;
    s += x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + x13 + x14;
    s += x0 * x1 * x2 * x3 * x4 * x5 * x6 * x7 * x8 * x9 * x10 * x11 * x12 * x13 * x14;
    if (s & 1)
        return s;
    return s + r[2] + x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + x13 + x14;
}

/* a slot and a sibling call: the epilogue before the branch gives the slot back */
NI int leaftail(int x)
{
    int v[4] = { x, x + 1, x + 2, x + 3 };
    int y = v[x & 3];

    if (y > 5)
        return id(y + 1);
    return y;
}

int main(void)
{
    static const int a[] = { 1, 2, 3, 2, 2, 5 };
    static const U p[] = { 1, 2, 3, 4, 5, 6 };
    U want = 0;

    if (classify(-5, 0) != -1 || classify(0, 1) != 0 || classify(3, 1) != 1 || classify(3, 0) != 2) return 1;
    if (classify(50, 2) != 5 || classify(500, 2) != 4) return 2;
    if (count(a, 6, 2) != 3 || count(a, 0, 2) != 0 || count(a, 6, 9) != 0) return 3;
    if (scan((const unsigned char *)" ab c ") != 3 + 1 + 3 || scan((const unsigned char *)"") != 0) return 4;
    {
        U a0 = 2, b0 = 3, c0 = 5, d0 = 7, r[6], s = 0, x[15];

        x[0] = a0 * 3, x[1] = b0 * 5, x[2] = c0 * 7, x[3] = d0 * 11, x[4] = a0 ^ b0, x[5] = c0 ^ d0, x[6] = a0 + d0;
        x[7] = b0 + c0, x[8] = a0 - c0, x[9] = b0 - d0, x[10] = a0 | d0, x[11] = b0 & c0, x[12] = a0 * b0;
        x[13] = c0 * d0, x[14] = a0 + b0 + c0;
        for (int i = 0; i < 6; i++)
            r[i] = p[i] + x[0] * i, s += r[i] ^ x[1];
        U m = 1, t = 0;
        for (int i = 0; i < 15; i++)
            t += x[i], m *= x[i];
        s += t + m;
        want = s & 1 ? s : s + r[2] + t;
    }
    if (leafy(2, 3, 5, 7, p) != want) return 5;
    sink = 0;
    if (leaftail(9) != 11 || leaftail(2) != 4) return 6;
    return 0;
}
