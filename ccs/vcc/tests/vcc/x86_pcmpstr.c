/* Native SSE4.2 oracle checks every immediate byte, result projection,
   implicit terminators, explicit signed lengths, and single evaluation. */
#if defined(__x86_64__)
typedef char bytes __attribute__((vector_size(16)));
static int evaluations;
__attribute__((noinline)) static bytes vector_arg(bytes x) { ++evaluations; return x; }
__attribute__((noinline)) static int length_arg(int x) { ++evaluations; return x; }
struct result { unsigned index; unsigned char flags[5], pad[7]; bytes mask; };
#define STR_(x) #x
#define STR(x) STR_(x)
#define REFERENCE(I, MODE, LENGTHS) \
    __asm__ volatile("movdqu %1, %%xmm8; movdqu %2, %%xmm9; " \
        "pcmp" MODE "stri $" STR(I) ", %%xmm9, %%xmm8; " \
        "movl %%ecx, 0(%0); seta 4(%0); setc 5(%0); seto 6(%0); sets 7(%0); setz 8(%0); " \
        "pcmp" MODE "strm $" STR(I) ", %2, %%xmm8; movdqu %%xmm0,16(%0)" \
        : : "r"(&ref), "m"(a), "m"(b) LENGTHS : "ecx", "xmm0", "xmm8", "xmm9", "cc", "memory")
#define NO_LENGTHS
#define LENGTHS , "a"(la), "d"(lb)
#define IARGS vector_arg(a), vector_arg(b)
#define EARGS vector_arg(a), length_arg(la), vector_arg(b), length_arg(lb)
#define CHECK(I, MODE, ARGS) do { \
    if (__builtin_ia32_pcmp##MODE##stri128(ARGS, (char)(I)) != (int)ref.index) return 1; \
    if (__builtin_ia32_pcmp##MODE##stria128(ARGS, (char)(I)) != ref.flags[0]) return 2; \
    if (__builtin_ia32_pcmp##MODE##stric128(ARGS, (char)(I)) != ref.flags[1]) return 3; \
    if (__builtin_ia32_pcmp##MODE##strio128(ARGS, (char)(I)) != ref.flags[2]) return 4; \
    if (__builtin_ia32_pcmp##MODE##stris128(ARGS, (char)(I)) != ref.flags[3]) return 5; \
    if (__builtin_ia32_pcmp##MODE##striz128(ARGS, (char)(I)) != ref.flags[4]) return 6; \
    bytes mask = __builtin_ia32_pcmp##MODE##strm128(ARGS, (char)(I)); \
    for (int j = 0; j < 16; j++) if (mask[j] != ref.mask[j]) return 7; \
} while (0)
#define CASE(I) case I: REFERENCE(I, "i", NO_LENGTHS); CHECK(I, i, IARGS); \
    REFERENCE(I, "e", LENGTHS); CHECK(I, e, EARGS); break
#define CASE16(B) \
    CASE(B); CASE(B+1); CASE(B+2); CASE(B+3); \
    CASE(B+4); CASE(B+5); CASE(B+6); CASE(B+7); \
    CASE(B+8); CASE(B+9); CASE(B+10); CASE(B+11); \
    CASE(B+12); CASE(B+13); CASE(B+14); CASE(B+15)
__attribute__((target("sse4.2"),noinline))
static int check(bytes a, bytes b, int la, int lb, int control)
{
    struct result ref;
    evaluations = 0;
    switch (control) {
    CASE16(0);
    CASE16(16);
    CASE16(32);
    CASE16(48);
    CASE16(64);
    CASE16(80);
    CASE16(96);
    CASE16(112);
    CASE16(128);
    CASE16(144);
    CASE16(160);
    CASE16(176);
    CASE16(192);
    CASE16(208);
    CASE16(224);
    CASE16(240);
    }
    return evaluations != 42 ? 8 : 0;
}
__attribute__((target("sse4.1"),noinline))
static int blend(bytes a, bytes b, int seed)
{
    bytes mask;
    for (int j = 0; j < 16; j++) mask[j] = (char)(seed * 16 + j);
    evaluations = 0;
    bytes out = __builtin_ia32_pblendvb128(vector_arg(a), vector_arg(b), vector_arg(mask));
    if (evaluations != 3) return 1;
    for (int j = 0; j < 16; j++)
        if (out[j] != (((unsigned char)mask[j] & 128) ? b[j] : a[j])) return 1;
    return 0;
}
int main(void)
{
    unsigned ax, bx, cx, dx;
    __asm__("cpuid" : "=a"(ax), "=b"(bx), "=c"(cx), "=d"(dx) : "0"(1), "2"(0));
    if (!(cx & (1u << 20))) return 0;
    unsigned random = 17;
    static const int lengths[] = { 0, 1, 7, 8, 15, 16, 17, -1, -7, -16, -17, 2147483647, (-2147483647 - 1) };
    for (int sample = 0; sample < 26; sample++) {
        bytes a, b;
        for (int j = 0; j < 16; j++) {
            random = random * 1664525u + 1013904223u; a[j] = (char)(random >> 24);
            random = random * 1664525u + 1013904223u; b[j] = (char)(random >> 24);
        }
        if (sample & 1) a[sample % 16] = 0;
        if (sample & 2) b[(sample * 3) % 16] = 0;
        if (sample & 4) a = b;
        if (sample & 8) { a[2] = a[3] = 0; b[14] = b[15] = 0; }
        if (sample == 0) a = b = (bytes){ 0 };
        if (sample == 1) a = (bytes){ 0 };
        if (blend(a, b, sample)) return 9;
        for (int mode = 0; mode < 256; mode++) {
            int r = check(a, b, lengths[sample % 13], lengths[(sample * 7) % 13], mode);
            if (r) return r;
        }
    }
    return 0;
}
#else
int main(void) { return 0; }
#endif
