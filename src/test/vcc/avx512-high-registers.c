/* More 512-bit values live than xmm0-xmm13: x86-64 VIR puts them in
 * zmm16-zmm31.  Values trading places on loop back edges and at joins, live
 * across calls, beside 128- and 256-bit vectors and doubles (which stay low),
 * splats from general registers, compares to masks, shifts by a count,
 * vpdpbusd and folded loads; results the same as clang's.
 * Does nothing on a CPU without AVX-512BW. */
int printf(const char *, ...);
#ifdef __x86_64__
#include <immintrin.h>
#define F __attribute__((noinline, target("avx512f,avx512bw")))

static unsigned long long hash(unsigned long long h, const void *p, int n)
{
    const unsigned char *c = (const unsigned char *)p;

    for (int i = 0; i < n; i++)
        h = h * 1099511628211ull + c[i];
    return h;
}

/* 24 live vectors, rotating, swapped on some iterations */
#define FOR24(M)                                                               \
    M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8) M(9) M(10) M(11) M(12)        \
    M(13) M(14) M(15) M(16) M(17) M(18) M(19) M(20) M(21) M(22) M(23)

F void rotate(__m512i *o, const __m512i *a, const __m512i *w, int k)
{
#define DECLARE(i) __m512i r##i = _mm512_load_si512(a + i);
    FOR24(DECLARE)
    for (int j = 0; j < k; j++) {
        __m512i x = _mm512_load_si512(w + (j & 7));
        __m512i t = r0;

#define STEP(i, n) r##i = _mm512_add_epi16(r##n, x);
        STEP(0, 1) STEP(1, 2) STEP(2, 3) STEP(3, 4) STEP(4, 5) STEP(5, 6)
        STEP(6, 7) STEP(7, 8) STEP(8, 9) STEP(9, 10) STEP(10, 11)
        STEP(11, 12) STEP(12, 13) STEP(13, 14) STEP(14, 15) STEP(15, 16)
        STEP(16, 17) STEP(17, 18) STEP(18, 19) STEP(19, 20) STEP(20, 21)
        STEP(21, 22) STEP(22, 23)
        r23 = _mm512_sub_epi16(t, x);
        if (j % 3 == 1) {
#define SWAP(i, n)                                                             \
    {                                                                          \
        __m512i u = r##i;                                                      \
        r##i = r##n;                                                           \
        r##n = u;                                                              \
    }
            SWAP(0, 23) SWAP(1, 17) SWAP(2, 16) SWAP(15, 22)
        }
        if (j % 5 == 2) {
            SWAP(3, 4) SWAP(4, 5) SWAP(18, 3)
        }
    }
#define SAVE(i) _mm512_store_si512(o + i, r##i);
    FOR24(SAVE)
}

static int calls;
__attribute__((noinline)) static void touch(void)
{
    calls++;
#ifdef __AVX512F__
    __asm__ volatile("vpxorq %%zmm16, %%zmm16, %%zmm16\n"
                     "vpxorq %%zmm31, %%zmm31, %%zmm31" ::: "xmm16", "xmm31");
#endif
}
/* a callee that writes the high registers itself */
F static void clobber(__m512i *o, const __m512i *a)
{
#define LOAD(i) __m512i r##i = _mm512_load_si512(a + (23 - i));
    FOR24(LOAD)
#define ADD(i) r##i = _mm512_add_epi32(r##i, r##i);
    FOR24(ADD)
#define OUT(i) _mm512_store_si512(o + i, r##i);
    FOR24(OUT)
}

F void across(__m512i *o, const __m512i *a, int k)
{
    static __m512i scratch[24];
    FOR24(DECLARE)

    for (int j = 0; j < k; j++) {
        touch();
        clobber(scratch, a);
#define MIX(i) r##i = _mm512_add_epi32(r##i, scratch[i]);
        FOR24(MIX)
        clobber(scratch, a);
    }
    FOR24(SAVE)
}

/* narrow values live among the wide ones */
F void mixed(__m512i *o, __m128i *n, __m256i *m, double *d, const __m512i *a,
             const __m128i *b, const __m256i *c, const double *e, int k)
{
    FOR24(DECLARE)
    __m128i x = b[0], y = b[1], z = b[2];
    __m256i u = c[0], v = c[1];
    double s = e[0], t = e[1];

    for (int j = 0; j < k; j++) {
        __m512i splat = _mm512_set1_epi16((short)(j * 77));
        __m512i splat32 = _mm512_set1_epi32(j * 1001);
        __m512i splat8 = _mm512_set1_epi8((char)j);
        __m512i splat64 = _mm512_set1_epi64(j * 0x100000001ll + k);

        x = _mm_add_epi32(x, y);
        y = _mm_xor_si128(y, z);
        z = _mm_add_epi16(z, x);
        u = _mm256_add_epi16(u, v);
        v = _mm256_xor_si256(v, u);
        s = s * 0.5 + t;
        t = t + 1.25;
#define WIDE(i, n) r##i = _mm512_add_epi16(r##i, r##n);
        WIDE(0, 1) WIDE(1, 2) WIDE(2, 3) WIDE(3, 4)
        r4 = _mm512_mulhi_epi16(r4, splat);
        r5 = _mm512_add_epi32(r5, splat32);
        r6 = _mm512_xor_si512(r6, splat8);
        r7 = _mm512_add_epi64(r7, splat64);
        r8 = _mm512_maddubs_epi16(r8, r9);
        r9 = _mm512_madd_epi16(r9, r10);
        r10 = _mm512_packus_epi16(r10, r11);
        r11 = _mm512_slli_epi16(r11, 3);
        r12 = _mm512_slli_epi16(r12, j & 7);
        r13 = _mm512_min_epi16(r13, r14);
        r14 = _mm512_max_epi16(r14, r15);
        r15 = _mm512_and_si512(r15, r16);
        r16 = _mm512_or_si512(r16, r17);
        r17 = _mm512_sub_epi16(r17, _mm512_load_si512(a + (j & 15)));
        r18 = _mm512_add_epi16(_mm512_load_si512(a + (j & 7)), r18);
        r19 = _mm512_srai_epi16(r19, 1);
        r20 = _mm512_add_epi32(r20, _mm512_set1_epi32(
                                        _mm512_cmpgt_epi32_mask(r21, r22)));
        r21 = _mm512_add_epi32(r21, r23);
        r22 = _mm512_add_epi8(r22, _mm512_setzero_si512());
        r23 = _mm512_adds_epi16(r23, r0);
    }
    FOR24(SAVE)
    n[0] = x;
    n[1] = y;
    n[2] = z;
    m[0] = u;
    m[1] = v;
    d[0] = s;
    d[1] = t;
}

__attribute__((noinline, target("avx512f,avx512bw,avx512vnni"))) static void
dots(__m512i *o, const __m512i *a, const __m512i *b, int k)
{
    FOR24(DECLARE)

    for (int j = 0; j < k; j++) {
        __m512i x = b[j & 3];

#define DOT(i, n) r##i = _mm512_dpbusd_epi32(r##i, x, r##n);
        DOT(0, 1) DOT(1, 2) DOT(2, 3) DOT(3, 4) DOT(4, 5) DOT(5, 6) DOT(6, 7)
        DOT(7, 8) DOT(8, 9) DOT(9, 10) DOT(10, 11) DOT(11, 12) DOT(12, 13)
        DOT(13, 14) DOT(14, 15) DOT(15, 16) DOT(16, 17) DOT(17, 18)
        DOT(18, 19) DOT(19, 20) DOT(20, 21) DOT(21, 22) DOT(22, 23)
        r23 = _mm512_dpbusd_epi32(r23, r0, _mm512_load_si512(a + (j & 15)));
        r5 = _mm512_dpbusd_epi32(r17, r5, r5);
    }
    FOR24(SAVE)
}

/* CPUID leaf 7: EBX bit 30 is AVX-512BW, ECX bit 11 AVX-512VNNI */
static unsigned cpuid7(int ecx)
{
    unsigned a, b, c, d;
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    if (ecx)
        return c;
    return b;
}

static void run(void)
{
    static __m512i a[24], w[16], o[24];
    static __m128i b[3], n[3];
    static __m256i c[2], m[2];
    static double e[2] = {1.5, -2.25}, d[2];
    unsigned long long h = 14695981039346656037ull;

    for (int t = 0; t < 40; t++) {
        unsigned char *p = (unsigned char *)a, *q = (unsigned char *)w;

        for (int i = 0; i < (int)sizeof a; i++)
            p[i] = (unsigned char)(i * 37 + t * 11 + (i >> 3));
        for (int i = 0; i < (int)sizeof w; i++)
            q[i] = (unsigned char)(i * 91 + t * 5 + (i >> 5));
        for (int i = 0; i < (int)sizeof b; i++)
            ((unsigned char *)b)[i] = (unsigned char)(i * 13 + t);
        for (int i = 0; i < (int)sizeof c; i++)
            ((unsigned char *)c)[i] = (unsigned char)(i * 7 + t);
        rotate(o, a, w, t);
        h = hash(h, o, sizeof o);
        across(o, a, t % 6);
        h = hash(h, o, sizeof o);
        mixed(o, n, m, d, a, b, c, e, t);
        h = hash(h, o, sizeof o);
        h = hash(h, n, sizeof n);
        h = hash(h, m, sizeof m);
        h = hash(h, d, sizeof d);
    }
    printf("%016llx %d\n", h, calls);
    if (cpuid7(1) >> 11 & 1) {
        dots(o, a, w, 13);
        printf("%016llx\n", hash(h, o, sizeof o));
    }
}

int main(void)
{
    if (cpuid7(0) >> 30 & 1)
        run();
    else
        printf("no AVX-512BW\n");
    return 0;
}
#else
int main(void)
{
    printf("no AVX-512BW\n");
    return 0;
}
#endif
