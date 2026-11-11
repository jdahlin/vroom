/* Mixed SIMD/memory constraints must remain usable under XMM pressure. */
#if defined(__x86_64__)
__attribute__((noinline)) static double sum(const double *p) {
    double r;
    __asm__("addsd %2,%0; addsd %3,%0; addsd %4,%0; addsd %5,%0;"
            "addsd %6,%0; addsd %7,%0; addsd %8,%0; addsd %9,%0;"
            "addsd %10,%0; addsd %11,%0"
            : "=&x"(r)
            : "0"(0.0), "xm"(p[0]), "xm"(p[1]), "xm"(p[2]),
              "xm"(p[3]), "xm"(p[4]), "xm"(p[5]), "xm"(p[6]),
              "xm"(p[7]), "xm"(p[8]), "xm"(p[9])
            : "xmm3", "xmm4", "xmm5", "xmm9", "xmm11", "xmm14");
    return r;
}
__attribute__((noinline)) static double temporary(double x) {
    double r = 0;
    __asm__("addsd %1,%0" : "+&x"(r) : "xm"(x + 3.0));
    return r;
}
#else
/* This tests x86 instruction constraints; no ARM asm contract is claimed. */
static double sum(const double *p) {
    double r=0; for (int i=0; i<10; i++) r+=p[i]; return r;
}
static double temporary(double x) { return x+3.0; }
#endif
int main(void) {
    double p[10];
    for (int j=0; j<4; j++) {
        for (int i=0; i<10; i++) p[i]=(i+1)*(j+1);
        if (sum(p) != 55.0*(j+1)) return 1;
    }
    return temporary(4.0) != 7.0;
}
