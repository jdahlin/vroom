/* The low half of a vector converted to lanes twice as wide, and vectors partly stored
   over zeros (_mm_cvtepu8_epi16, _mm_loadl_epi64 and their like). */
int printf(const char *, ...);

typedef signed char s8 __attribute__((vector_size(16)));
typedef unsigned char u8 __attribute__((vector_size(16)));
typedef short s16 __attribute__((vector_size(16)));
typedef unsigned short u16 __attribute__((vector_size(16)));
typedef int s32 __attribute__((vector_size(16)));
typedef unsigned u32 __attribute__((vector_size(16)));
typedef long long s64 __attribute__((vector_size(16)));

static void show(const char *name, const void *p)
{
    const unsigned char *c = p;
    printf("%s ", name);
    for (int i = 0; i < 16; i++)
        printf("%02x", c[i]);
    printf("\n");
}

#define LOW8(v) __builtin_shufflevector(v, v, 0, 1, 2, 3, 4, 5, 6, 7)
#define LOW4(v) __builtin_shufflevector(v, v, 0, 1, 2, 3)
#define LOW2(v) __builtin_shufflevector(v, v, 0, 1)

__attribute__((noinline)) static void widen(const void *p)
{
    s8 a = *(const s8 *)p;
    u8 b = *(const u8 *)p;
    s16 r1 = __builtin_convertvector(LOW8(a), s16);
    s16 r2 = __builtin_convertvector(LOW8(b), s16);
    u16 r3 = __builtin_convertvector(LOW8(b), u16);
    s32 r4 = __builtin_convertvector(LOW4((s16)a), s32);
    u32 r5 = __builtin_convertvector(LOW4((u16)a), u32);
    s64 r6 = __builtin_convertvector(LOW2((s32)a), s64);
    s64 r7 = __builtin_convertvector(LOW2((u32)a), s64);
    show("s8", &r1);
    show("u8s", &r2);
    show("u8u", &r3);
    show("s16", &r4);
    show("u16", &r5);
    show("s32", &r6);
    show("u32", &r7);
}

__attribute__((noinline)) static void partial(long long x, long long y, int w)
{
    s64 low = {x, 0};
    s64 one = {x};
    union {
        s64 v;
        long long q[2];
        int i[4];
    } u = {{0, 0}};
    u.q[0] = x;
    u.q[1] = y;
    u.q[1] = x ^ y;
    s64 twice = u.v;
    union {
        s64 v;
        long long q[2];
        int i[4];
    } h = {{0, 0}};
    h.q[0] = x;
    h.i[2] = w;
    s64 mixed = h.v;
    show("low", &low);
    show("one", &one);
    show("twice", &twice);
    show("mixed", &mixed);
}

/* zeros that a vector store, or a store through a pointer, has since replaced */
__attribute__((noinline)) static void replaced(s64 whole, long long y, int k)
{
    union {
        s64 v;
        long long q[2];
    } z = {{0, 0}};
    z.v = whole;
    z.q[1] = y;
    s64 vector_first = z.v;
    union {
        s64 v;
        long long q[2];
    } p = {{0, 0}}, other = {{0, 0}};
    long long *through = &other.q[0];
    if (k)
        through = &p.q[1];
    p.q[0] = y;
    *through = 41;
    p.q[0] = y + 1;
    s64 pointed = p.v;
    show("vector", &vector_first);
    show("pointer", &pointed);
    show("other", &other);
}

int main(void)
{
    unsigned char data[32];
    for (int i = 0; i < 32; i++)
        data[i] = (unsigned char)(i * 73 + 120);
    widen(data);
    widen(data + 16);
    partial(0x1122334455667788LL, -1, 77);
    partial(-5, 9, -1);
    replaced((s64){7, 8}, 99, 1);
    replaced((s64){-7, -8}, -99, 0);
    return 0;
}
