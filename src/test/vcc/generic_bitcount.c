/* Generic counts preserve exact precision and evaluate every argument once. */
#define W(N) _Static_assert(__builtin_clzg((unsigned _BitInt(N))1)==N-1, "clz"); _Static_assert(__builtin_popcountg((unsigned _BitInt(N))-1)==N,"pop"); _Static_assert(__builtin_ctzg((unsigned _BitInt(N))0,N)==N,"zero")
W(1); W(7); W(8); W(9); W(16); W(32); W(64); W(17); W(33); W(63); W(65); W(127); W(128);

extern void abort(void);
#define CHECK(x) do { if (!(x)) abort(); } while (0)
static int calls;
static unsigned __int128 value(unsigned i) { ++calls; return (unsigned __int128)1 << i; }
int main(void)
{
    for (unsigned i = 0; i < 128; ++i) {
        unsigned __int128 x = (unsigned __int128)1 << i;
        CHECK(__builtin_clzg(x) == 127 - (int)i);
        CHECK(__builtin_ctzg(x) == (int)i);
        CHECK(__builtin_popcountg(x) == 1);
        CHECK(__builtin_popcountg(~x) == 127);
        calls = 0;
        CHECK(__builtin_clzg(value(i)) == 127 - (int)i && calls == 1);
        calls = 0;
        CHECK(__builtin_ctzg(value(i)) == (int)i && calls == 1);
        calls = 0;
        CHECK(__builtin_popcountg(value(i)) == 1 && calls == 1);
    }
    for (int i = 0; i < 2; ++i) {
        unsigned __int128 x = i;
        int y = 128;
        CHECK(__builtin_clzg(x++, y++) == (i ? 127 : 128));
        CHECK(x == (unsigned)(i + 1) && y == 129);
        x = i; y = 128;
        CHECK(__builtin_ctzg(x++, y++) == (i ? 0 : 128));
        CHECK(x == (unsigned)(i + 1) && y == 129);
    }
    /* Runtime precision differs from allocation size for these types. */
#define R(N) do { \
    volatile unsigned _BitInt(N) z = 1, all = (unsigned _BitInt(N))-1; \
    CHECK(__builtin_clzg(z) == (N)-1); \
    CHECK(__builtin_ctzg(z) == 0); \
    CHECK(__builtin_popcountg(all) == N); \
    z = (unsigned _BitInt(N))1 << ((N)-1); \
    CHECK(__builtin_clzg(z) == 0 && __builtin_ctzg(z) == (N)-1); \
    z = 0; CHECK(__builtin_clzg(z, -23) == -23 && __builtin_ctzg(z, -24) == -24); \
} while (0)
    R(1); R(7); R(9); R(17); R(31); R(32); R(33); R(63); R(64); R(65); R(127); R(128);
    volatile _Bool truth = 1;
    CHECK(__builtin_clzg(truth) == 0 && __builtin_ctzg(truth) == 0 && __builtin_popcountg(truth) == 1);
    truth = 0;
    CHECK(__builtin_clzg(truth, (short)123) == 123);
    CHECK(__builtin_ctzg(truth, (unsigned char)124) == 124);
    struct { unsigned value : 3; } narrow = {5};
    CHECK(__builtin_clzg(truth, narrow.value) == 5);
    volatile unsigned char c = 1;
    volatile unsigned short s = 1;
    volatile unsigned int u = 1;
    volatile unsigned long long l = 1;
    CHECK(__builtin_clzg(c) == 7 && __builtin_clzg(s) == 15);
    CHECK(__builtin_clzg(u) == 31 && __builtin_clzg(l) == 63);
    CHECK(__builtin_ctzg(c) == 0 && __builtin_popcountg(c) == 1);
    return 0;
}
