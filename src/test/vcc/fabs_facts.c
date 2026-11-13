extern double fabs(double);
extern float fabsf(float);
extern double copysign(double, double);
static int calls;
__attribute__((noinline)) static double input(double x) { ++calls; return x; }
__attribute__((noinline)) static int nonnegative(double x) {
    double p = fabs(input(x));
    float q = fabsf((float)input(x));
    double zero = 0;
    return p < zero || q < 0;
}
__attribute__((noinline)) static int check(double x) {
    double p = fabs(input(x));
    float q = fabsf((float)input(x));
    double z = 0;
    if (p < z || q < 0) return 1;
    p = -1;
    if (!(p < z)) return 2;
    p = copysign(1.0, x);
    if (x < 0 && !(p < z)) return 3;
    return 0;
}
int main(void) {
    if (nonnegative(-2) || nonnegative(-0.0) || nonnegative(__builtin_nan(""))) return 1;
    if (check(-2) || check(0) || check(__builtin_nan(""))) return 2;
    return calls != 12;
}
