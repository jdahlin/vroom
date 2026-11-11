/* Constant initializers and runtime division must survive exponent limits. */
static _Complex double huge = (0x1p1000 + 0x1p1000i) / (0x1p1000 + 0x1p1000i);
static _Complex double tiny = (0x1p-1000 + 0x1p-1000i) / (0x1p-1000 + 0x1p-1000i);
static _Complex long double wide = (0x1p16000L + 0x1p16000Li) / (0x1p16000L + 0x1p16000Li);
static _Complex double infinite = (__builtin_inf() + 1.0i) / (1.0 + 1.0i);
static _Complex double zero = (1.0 + 2.0i) / (__builtin_inf() + 1.0i);
static _Complex double singular = (1.0 + 2.0i) / (0.0 + 0.0i);
/* Each arithmetic operation rounds at the target precision, as clang does. */
static _Complex double rounded =
    (0x1.f767c482c9b00p+517 - 0x1.21b822346e320p+413i) /
    (0x1.c511afebb6a18p+515 + 0x1.375c64ec1d7d8p+84i);
static int calls;
static _Complex double operand(double x) { calls++; return x + x * 1.0i; }
static _Complex float dividef(_Complex float a, _Complex float b) { return a / b; }
static _Complex long double dividel(_Complex long double a, _Complex long double b) { return a / b; }
int main(void)
{
    if (__real__ huge != 1 || __imag__ huge != 0 ||
        __real__ tiny != 1 || __imag__ tiny != 0 ||
        __real__ wide != 1 || __imag__ wide != 0) return 1;
    _Complex double z = operand(0x1p1000) / operand(0x1p1000);
    if (calls != 2 || __real__ z != 1 || __imag__ z != 0) return 2;
    z = operand(0x1p-1000) / operand(0x1p-1000);
    if (calls != 4 || __real__ z != 1 || __imag__ z != 0) return 3;
    _Complex float f = dividef(0x1p100f + 0x1p100fi, 0x1p100f + 0x1p100fi);
    if (__real__ f != 1 || __imag__ f != 0) return 4;
    _Complex long double l = dividel(0x1p16000L + 0x1p16000Li, 0x1p16000L + 0x1p16000Li);
    if (__real__ l != 1 || __imag__ l != 0) return 5;
    if (!__builtin_isinf(__real__ infinite) || !__builtin_isinf(__imag__ infinite) ||
        !__builtin_signbit(__imag__ infinite)) return 6;
    if (__real__ zero != 0 || __imag__ zero != 0) return 7;
    if (!__builtin_isinf(__real__ singular) || !__builtin_isinf(__imag__ singular)) return 8;
    if (__imag__ rounded != -0x1.476737b3ca580p-103) return 9;
    return 0;
}
