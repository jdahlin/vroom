/* The results of calls made while cleanups are live (invokes in VIR): used in later blocks, across
   other such calls, many at once (so that some live in callee-saved registers and some are
   spilled), and a float result; the normal path only, no exception (vir_alloc assigns an
   invoke's result at the end of its block, and its lowering stores a spilled one before the
   branch). */
static int log_n;

static void note(int *p)
{
    log_n += *p;
}

__attribute__((noinline)) static long id(long x)
{
    return x;
}

__attribute__((noinline)) static double half(double x)
{
    return x / 2;
}

__attribute__((noinline)) static long many(int k)
{
    int guard __attribute__((cleanup(note))) = 1;
    long a = id(k + 1), b = id(k + 2), c = id(k + 3), d = id(k + 4),
         e = id(k + 5), f = id(k + 6), g = id(k + 7), h = id(k + 8),
         i = id(k + 9), j = id(k + 10), l = id(k + 11), m = id(k + 12);
    long s = 0;

    for (int r = 0; r < 3; r++)
        s += id(a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 +
                i * 9 + j * 10 + l * 11 + m * 12 + r);
    return s;
}

__attribute__((noinline)) static double floats(double x)
{
    int guard __attribute__((cleanup(note))) = 2;
    double a = half(x), b = half(a), c = half(b);

    return a + b * 10 + c * 100;
}

int main(void)
{
    long want = 0;

    for (int r = 0; r < 3; r++) {
        long t = 0;
        for (int q = 1; q <= 12; q++)
            t += (long)q * (q + 5);
        want += t + r;
    }
    if (many(5) != want)
        return 1;
    if (floats(8) != 4 + 2 * 10 + 1 * 100)
        return 2;
    if (log_n != 3)
        return 3;
    return 0;
}
