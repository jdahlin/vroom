// [ext:gnu_library_builtins] libc++'s <cstring> and <cmath>
// builtins: <math.h>'s functions (f and l forms too), the string
// searches, and the C front end's type-generic
// classifications, whose argument is not promoted (a float subnormal
// stays one)
int strings(const char *s) {
    if (__builtin_strpbrk(s, "lo") != s + 2 || __builtin_strrchr(s, 'l') != s + 3)
        return 1;
    if (__builtin_strstr(s, "llo") != s + 2 || __builtin_memchr(s, 'e', 5) != s + 1)
        return 2;
    return 0;
}

int maths(double d, float f, long double l) {
    if (__builtin_sqrt(d) != 2.0 || __builtin_sqrtf(f) != 3.0f || __builtin_fabsl(-l) != l)
        return 10;
    if (__builtin_pow(d, 2) != 16.0 || __builtin_floor(2.5) != 2.0 || __builtin_trunc(-2.5) != -2.0)
        return 11;
    int e = 0;
    double frac = __builtin_frexp(d, &e);
    if (frac != 0.5 || e != 3 || __builtin_ldexp(frac, e) != d)
        return 12;
    if (__builtin_copysign(d, -1.0) != -4.0 || __builtin_fmod(9.0, d) != 1.0)
        return 13;
    return 0;
}

int classify(float f, double d) {
    float tiny = 1e-40f;
    if (!__builtin_isfinite(f) || __builtin_isinf(d) || __builtin_isnan(f))
        return 20;
    if (__builtin_isnormal(tiny) || !__builtin_isnormal(f) || !__builtin_signbit(-f))
        return 21;
    if (__builtin_fpclassify(0, 1, 2, 3, 4, tiny) != 3 || !__builtin_isless(f, d))
        return 22;
    return 0;
}

int main() {
    if (int r = strings("hello"))
        return r;
    if (int r = maths(4.0, 9.0f, 25))
        return r;
    return classify(1.5f, 2.0);
}
