/* Scalar/complex return conversions; GCC dg torture/pr116454-1 exposed a
   scalar zero being treated as an aggregate address instead of complex zero. */
extern void abort(void);
static volatile int calls;
static int next(void) { calls++; return 7; }
__attribute__((noinline)) static _Complex float zero(void) { return 0; }
__attribute__((noinline)) static _Complex float from_int(void) { return next(); }
__attribute__((noinline)) static _Complex double from_real(double x) { return x; }
__attribute__((noinline)) static _Complex float narrow(_Complex double x) { return x; }
__attribute__((noinline)) static _Complex int from_int_complex(int x) { return x; }

int main(void)
{
    _Complex float z = zero();
    if (__real__ z != 0 || __imag__ z != 0) abort();
    z = from_int();
    if (__real__ z != 7 || __imag__ z != 0 || calls != 1) abort();
    _Complex double d = from_real(2.5);
    if (__real__ d != 2.5 || __imag__ d != 0) abort();
    z = narrow(3.25 + 1.5i);
    if (__real__ z != 3.25f || __imag__ z != 1.5f) abort();
    _Complex int i = from_int_complex(-9);
    if (__real__ i != -9 || __imag__ i != 0) abort();
    return 0;
}
