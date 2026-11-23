/* an unsigned carry or borrow test reads the flags of its addition or subtraction at -O2 (VIR's carry placement, the lowerings' adds/subs and add/sub): every shape of the test, 32 and 64 bits, a carry chain, the test as a branch and as a value, operands that are the same value, constants; -fno-carry-flags gives the same answers */
#include <assert.h>
#define NI __attribute__((noinline))
typedef unsigned long long u64;
typedef unsigned u32;

NI int c64_sum_lt_x(u64 x, u64 y, u64 *s) { *s = x + y; return *s < x; }
NI int c64_sum_lt_y(u64 x, u64 y, u64 *s) { *s = x + y; return *s < y; }
NI int c64_x_gt_sum(u64 x, u64 y, u64 *s) { *s = x + y; return x > *s; }
NI int c64_sum_ge_x(u64 x, u64 y, u64 *s) { *s = x + y; return *s >= x; }
NI int c64_y_le_sum(u64 x, u64 y, u64 *s) { *s = x + y; return y <= *s; }
NI int b64_x_lt_y(u64 x, u64 y, u64 *d) { *d = x - y; return x < y; }
NI int b64_y_gt_x(u64 x, u64 y, u64 *d) { *d = x - y; return y > x; }
NI int b64_x_ge_y(u64 x, u64 y, u64 *d) { *d = x - y; return x >= y; }
NI int b64_y_le_x(u64 x, u64 y, u64 *d) { *d = x - y; return y <= x; }
NI int c32_sum_lt_x(u32 x, u32 y, u32 *s) { *s = x + y; return *s < x; }
NI int c32_x_gt_sum(u32 x, u32 y, u32 *s) { *s = x + y; return x > *s; }
NI int b32_x_lt_y(u32 x, u32 y, u32 *d) { *d = x - y; return x < y; }
NI int b32_x_ge_y(u32 x, u32 y, u32 *d) { *d = x - y; return x >= y; }
/* the test after other work, and the other word of the pair first */
NI int c64_late(u64 x, u64 y, u64 z, u64 *s, u64 *t) { u64 a = x + y, b = a + z; *s = a; *t = b; return (a < x) | (b < a); }
NI int b64_late(u64 x, u64 y, u64 z, u64 *s, u64 *t) { u64 a = x - y, b = a - z; *s = a; *t = b; return (x < y) | (a < z); }
/* as a branch */
NI u64 sat_add(u64 x, u64 y) { u64 s = x + y; if (s < x) return ~0ull; return s; }
NI u64 sat_sub(u64 x, u64 y) { u64 d = x - y; if (x < y) return 0; return d; }
NI u32 sat_add32(u32 x, u32 y) { u32 s = x + y; if (s < y) s = ~0u; return s; }
/* the two operands one value; a constant operand; the sum unused but for the test */
NI int twice(u64 x, u64 *s) { *s = x + x; return *s < x; }
NI int plus_const(u64 x, u64 *s) { *s = x + 100; return *s < x; }
NI int minus_const(u64 x, u64 *d) { *d = x - 100; return x < 100; }
NI int only_test(u64 x, u64 y) { return x + y < x; }
/* a signed test of the same values is no carry */
NI int signed_lt(long long x, long long y, long long *s) { *s = (long long)((u64)x + (u64)y); return *s < x; }
/* a chain: 4 words */
NI u64 add4(u64 *r, const u64 *a, const u64 *b) { u64 c = 0; for (int i = 0; i < 4; i++) { u64 s = a[i] + b[i], t = s + c; c = (s < a[i]) | (t < s); r[i] = t; } return c; }
NI u64 sub4(u64 *r, const u64 *a, const u64 *b) { u64 c = 0; for (int i = 0; i < 4; i++) { u64 d = a[i] - b[i], t = d - c; c = (a[i] < b[i]) | (d < c); r[i] = t; } return c; }
/* the test's value used twice, and both the test and its negation */
NI int both(u64 x, u64 y, int *n) { u64 s = x + y; int c = s < x; *n = s >= x; return c + c; }

static const u64 V64[] = {0, 1, 2, 99, 100, 101, 0x7fffffffffffffffull, 0x8000000000000000ull, 0x8000000000000001ull, 0xfffffffffffffffeull, 0xffffffffffffffffull, 0x00000000ffffffffull, 0x0000000100000000ull};
static const u32 V32[] = {0, 1, 2, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xfffffffeu, 0xffffffffu};

int main(void)
{
    for (unsigned i = 0; i < sizeof V64 / sizeof *V64; i++)
        for (unsigned j = 0; j < sizeof V64 / sizeof *V64; j++) {
            u64 x = V64[i], y = V64[j], r, t;
            unsigned __int128 wide = (unsigned __int128)x + y;
            int carry = (int)(wide >> 64), borrow = x < y, n;

            assert(c64_sum_lt_x(x, y, &r) == carry && r == (u64)wide);
            assert(c64_sum_lt_y(x, y, &r) == carry && r == (u64)wide);
            assert(c64_x_gt_sum(x, y, &r) == carry && r == (u64)wide);
            assert(c64_sum_ge_x(x, y, &r) == !carry && r == (u64)wide);
            assert(c64_y_le_sum(x, y, &r) == !carry && r == (u64)wide);
            assert(b64_x_lt_y(x, y, &r) == borrow && r == x - y);
            assert(b64_y_gt_x(x, y, &r) == borrow && r == x - y);
            assert(b64_x_ge_y(x, y, &r) == !borrow && r == x - y);
            assert(b64_y_le_x(x, y, &r) == !borrow && r == x - y);
            assert(sat_add(x, y) == (carry ? ~0ull : x + y));
            assert(sat_sub(x, y) == (borrow ? 0 : x - y));
            assert(only_test(x, y) == carry);
            assert(both(x, y, &n) == 2 * carry && n == !carry);
            assert(signed_lt((long long)x, (long long)y, (long long *)&r) == ((long long)(x + y) < (long long)x));
            for (unsigned k = 0; k < sizeof V64 / sizeof *V64; k++) {
                u64 z = V64[k];
                unsigned __int128 w3 = (unsigned __int128)x + y + z;

                assert(c64_late(x, y, z, &r, &t) == (w3 >> 64 != 0) && r == x + y && t == (u64)w3);
                assert(b64_late(x, y, z, &r, &t) == ((unsigned __int128)x < (unsigned __int128)y + z) && r == x - y && t == x - y - z);
            }
        }
    for (unsigned i = 0; i < sizeof V64 / sizeof *V64; i++) {
        u64 x = V64[i], r;

        assert(twice(x, &r) == (x >> 63 != 0) && r == x + x);
        assert(plus_const(x, &r) == (x > ~0ull - 100) && r == x + 100);
        assert(minus_const(x, &r) == (x < 100) && r == x - 100);
    }
    for (unsigned i = 0; i < sizeof V32 / sizeof *V32; i++)
        for (unsigned j = 0; j < sizeof V32 / sizeof *V32; j++) {
            u32 x = V32[i], y = V32[j], r;
            int carry = (int)(((u64)x + y) >> 32), borrow = x < y;

            assert(c32_sum_lt_x(x, y, &r) == carry && r == x + y);
            assert(c32_x_gt_sum(x, y, &r) == carry && r == x + y);
            assert(b32_x_lt_y(x, y, &r) == borrow && r == x - y);
            assert(b32_x_ge_y(x, y, &r) == !borrow && r == x - y);
            assert(sat_add32(x, y) == (carry ? ~0u : x + y));
        }
    {
        u64 a[4] = {~0ull, ~0ull, ~0ull, 1}, b[4] = {1, 0, 0, 2}, r[4];

        assert(add4(r, a, b) == 0 && r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 4);
        a[3] = ~0ull;
        assert(add4(r, a, b) == 1 && r[3] == 2);
        u64 p[4] = {0, 0, 0, 5}, q[4] = {1, 0, 0, 2};
        assert(sub4(r, p, q) == 0 && r[0] == ~0ull && r[1] == ~0ull && r[2] == ~0ull && r[3] == 2);
        p[3] = 2;
        assert(sub4(r, p, q) == 1 && r[3] == ~0ull);
    }
    return 0;
}
