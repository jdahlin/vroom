/* a floating parameter whose only writes are non-negative constants still
   holds any value on entry: `if (a < 0) a = 0;` is a test */
int printf(const char *, ...);

__attribute__((noinline)) static float low(float a)
{
    if (a < 0) {
        a = 0;
    }
    return a;
}

__attribute__((noinline)) static double other(double a, double z)
{
    if (a < z)
        a = 2;
    if (a < 0)
        a = 3;
    return a;
}

static float inlined(float a)
{
    if (a < 0.0f)
        a = 0.0f;
    return a;
}

int main(void)
{
    volatile float v = -5;

    printf("%g %g %g %g %g\n", low(v), low(7), other(v, 0), other(v, -9),
           inlined(v));
    return 0;
}
