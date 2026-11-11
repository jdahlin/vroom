/* One identical reference-built caller for every audited object. */
#include <stdio.h>
#include <stdint.h>
typedef unsigned vec __attribute__((vector_size(16)));
unsigned oa_fold(void), oa_array_const(void);
unsigned oa_local_const(unsigned), oa_inline(unsigned), oa_always(unsigned);
unsigned oa_auto_inline(unsigned), oa_identity(unsigned), oa_popcount(unsigned long);
unsigned oa_add32(unsigned, unsigned), oa_cse(unsigned, unsigned), oa_range(unsigned);
unsigned oa_sum(const unsigned *, unsigned), oa_licm(const unsigned *, unsigned, unsigned, unsigned);
unsigned oa_dead_local(unsigned), oa_aggregate(unsigned, unsigned), oa_cond_fold(unsigned);
unsigned long oa_div8(unsigned long), oa_mod8(unsigned long), oa_div10(unsigned long), oa_mul9(unsigned long);
long oa_sdiv8(long);
int oa_indexed(const int *, unsigned long), oa_reload(const int *), oa_tail(int), oa_switch(unsigned);
int oa_volatile(const volatile int *);
void oa_stores(int *), oa_vector_add(vec *, const vec *, const vec *);
void oa_copy(unsigned *, const unsigned *, unsigned);
int other(int x) { return x * 3 + 1; }

int main(void) {
    uint64_t h = 0, state = 1234567;
    unsigned a[64], b[64];
    int indexed[67];
    for (unsigned i = 0; i < 64; i++) a[i] = i * 31337u;
    for (unsigned i = 0; i < 67; i++) indexed[i] = (int)i - 20;
    /* Boundary checks remain separate from the differential checksum. */
    const unsigned long edges[] = { 0, 1, 7, 8, 9, 10, 11, 15, 16, 17,
        ~0ul, ~0ul - 1, 1ul << 63, (1ul << 63) - 1 };
    for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
        unsigned long x = edges[i];
        if (oa_div8(x) != x / 8 || oa_mod8(x) != x % 8 ||
            oa_div10(x) != x / 10 || oa_mul9(x) != x * 9) return 1;
    }
    const long signed_edges[] = { 0, 1, -1, 7, -7, 8, -8, 9, -9,
        (-9223372036854775807L - 1), 9223372036854775807L };
    for (unsigned i = 0; i < sizeof(signed_edges) / sizeof(signed_edges[0]); i++)
        if (oa_sdiv8(signed_edges[i]) != signed_edges[i] / 8) return 1;
    for (unsigned i = 0; i < 512; i++) {
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        unsigned x = (unsigned)state, y = (unsigned)(state >> 32), n = i % 65;
        int p = (int)(i % 100);
        vec va = { x, y, x + 1, y + 2 }, vb = { y, x, 7, 9 }, vd;
#define MIX(v) (h = (h ^ (uint64_t)(v)) * UINT64_C(1099511628211))
        MIX(oa_fold()); MIX(oa_array_const()); MIX(oa_local_const(x));
        MIX(oa_inline(x)); MIX(oa_always(x)); MIX(oa_auto_inline(x)); MIX(oa_identity(x));
        MIX(oa_div8(state)); MIX(oa_mod8(state)); MIX(oa_div10(state)); MIX(oa_mul9(state));
        MIX(oa_sdiv8(-(long)(state >> 1))); MIX(oa_sdiv8((long)(state >> 1)));
        MIX(oa_indexed(indexed, i % 64)); MIX(oa_add32(x, y)); MIX(oa_cse(x, y));
        MIX(oa_range(i % 30)); MIX(oa_reload(&p)); MIX(oa_volatile(&p)); oa_stores(&p); MIX(p);
        MIX(oa_tail((int)(i % 100))); MIX(oa_popcount(state)); MIX(oa_switch(i % 10));
        oa_vector_add(&vd, &va, &vb);
        for (unsigned j = 0; j < 4; j++) MIX(vd[j]);
        MIX(oa_sum(a, n)); MIX(oa_licm(a, n, i % 11, i % 7));
        oa_copy(b, a, n);
        for (unsigned j = 0; j < n; j++) MIX(b[j]);
        MIX(oa_dead_local(x)); MIX(oa_aggregate(x, y)); MIX(oa_cond_fold(x));
    }
    printf("%016llx\n", (unsigned long long)h);
    return 0;
}
