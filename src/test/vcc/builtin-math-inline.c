/* sqrt, fmin and fmax computed in place when optimizing, as clang: the right
 * values for NaN, infinities and negative arguments, arguments evaluated once.
 * The test runtime has no libm: at -O0, where these stay library calls, the
 * test checks nothing. */
double sqrt(double);
float sqrtf(float);
double fmin(double, double), fmax(double, double);
float fminf(float, float), fmaxf(float, float);
#define INFINITY __builtin_inf()
#define NAN __builtin_nan("")

#ifdef __OPTIMIZE__
static int calls;
static double next(void) { return ++calls * 4.0; }
__attribute__((noinline)) static int roots(double x, float y)
{
    double a = sqrt(x);
    float b = sqrtf(y);
    int bad = 0;
    if (x < 0 || x != x)
        bad |= a == a;
    else if (x < 1e200)
        bad |= a * a < x * 0.999999 || a * a > x * 1.000001;
    if (y < 0 || y != y)
        bad |= b == b;
    return bad;
}
/* what C says, written out: a NaN gives the other argument */
__attribute__((noinline)) static int same(double got, double x, double y,
                                          int larger)
{
    double want = x;
    if (x != x)
        want = y;
    else if (y == y && (larger ? y > x : y < x))
        want = y;
    return got == want || (got != got && want != want);
}
__attribute__((noinline)) static int extremes(double x, double y, float u,
                                              float v)
{
    return !same(fmin(x, y), x, y, 0) || !same(fmax(x, y), x, y, 1) ||
           !same(fminf(u, v), u, v, 0) || !same(fmaxf(u, v), u, v, 1) ||
           !same(__builtin_fmin(x, 1.0), x, 1.0, 0) ||
           !same(__builtin_fmax(y, 1.0), y, 1.0, 1);
}
int main(void)
{
    double v[] = {4.0, 2.25, 0.0, -1.0, INFINITY, -INFINITY, NAN, 1e300};
    for (int i = 0; i < 8; i++) {
        if (roots(v[i], (float)v[(i + 3) % 8]))
            return 1;
        for (int j = 0; j < 8; j++)
            if (extremes(v[i], v[j], (float)v[j], (float)v[i]))
                return 2;
    }
    double r = sqrt(next()) + sqrt(next());
    if (calls != 2 || r < 4.8 || r > 4.9)
        return 3;
    r = fmax(next(), next());
    if (calls != 4 || r != 16.0)
        return 4;
    return 0;
}
#else
int main(void) { return 0; }
#endif
