#if defined(__x86_64__)
typedef float v4f __attribute__((vector_size(16)));
typedef int v4i __attribute__((vector_size(16)));
typedef short v8s __attribute__((vector_size(16)));
static int calls;
static v4f arg(v4f x) { calls++; return x; }
static int shift_count;
static int count(void) { calls++; return shift_count; }
int main(void)
{
    v4f a = {1,2,3,4}, b = {5,6,7,8};
    v4f s = __builtin_ia32_shufps(arg(a), arg(b), 0x1b);
    if (calls != 2 || s[0] != 4 || s[1] != 3 || s[2] != 6 || s[3] != 5) return 1;
    a = (v4f){__builtin_nanf(""), 0.0f, -0.0f, 7};
    b = (v4f){5, -0.0f, 0.0f, __builtin_nanf("")};
    s = __builtin_ia32_maxps(arg(a), arg(b));
    if (calls != 4 || s[0] != 5 || !__builtin_signbit(s[1]) || __builtin_signbit(s[2]) || !__builtin_isnan(s[3])) return 2;
    s = __builtin_ia32_minps(a, b);
    if (s[0] != 5 || !__builtin_signbit(s[1]) || __builtin_signbit(s[2]) || !__builtin_isnan(s[3])) return 3;
    v4i mask = (v4i)__builtin_ia32_cmpnleps(a,b);
    if (mask[0] != -1 || mask[1] != 0 || mask[2] != 0 || mask[3] != -1) return 4;
    v8s x = {-1, 0x1234, -32768, 1, -1, 0, 7, 127};
    shift_count = 8;
    v8s left = __builtin_ia32_psllwi128(x,count());
    v8s right = __builtin_ia32_psrlwi128(x,count());
    if (calls != 6 || (unsigned short)left[0] != 0xff00 || left[1] != 0x3400 || left[2] != 0 || right[0] != 255 || right[2] != 128) return 5;
    shift_count = 16; left = __builtin_ia32_psllwi128(x,count());
    shift_count = -1; right = __builtin_ia32_psrlwi128(x,count());
    for (int i=0; i<8; i++) if (left[i] || right[i]) return 6;
    if (calls != 8) return 7;
    a = (v4f){-0.0f, 1, -__builtin_nanf(""), -5};
    if (__builtin_ia32_movmskps(arg(a)) != 13 || calls != 9) return 8;
    return 0;
}
#else
int main(void) { return 0; }
#endif
