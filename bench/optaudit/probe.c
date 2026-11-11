/* Small, defined-behaviour examples for vcc-optaudit.py, not a speed benchmark.
   Keep functions externally visible so every compiler must emit each example. */
typedef unsigned vec __attribute__((vector_size(16)));

unsigned oa_fold(void) { return (17u * 9u + 3u) ^ 11u; }
unsigned oa_local_const(unsigned x) { unsigned k = 7; return x + k; }
unsigned oa_array_const(void) { unsigned a[2]; a[0] = 17; a[1] = 9; return a[0] + a[1]; }
static inline unsigned inline_helper(unsigned x) { return x * 3 + 1; }
static __attribute__((always_inline)) inline unsigned always_helper(unsigned x) { return x * 3 + 1; }
static unsigned auto_helper(unsigned x) { return x * 3 + 1; }
unsigned oa_inline(unsigned x) { return inline_helper(x); }
unsigned oa_always(unsigned x) { return always_helper(x); }
unsigned oa_auto_inline(unsigned x) { return auto_helper(x); }
unsigned oa_identity(unsigned x) { return (x + 0u) * 1u; }
unsigned long oa_div8(unsigned long x) { return x / 8; }
unsigned long oa_mod8(unsigned long x) { return x % 8; }
long oa_sdiv8(long x) { return x / 8; }
unsigned long oa_div10(unsigned long x) { return x / 10; }
unsigned long oa_mul9(unsigned long x) { return x * 9; }
int oa_indexed(const int *p, unsigned long i) { return p[i + 3]; }
unsigned oa_add32(unsigned a, unsigned b) { return a + b; }
void oa_stores(int *p) { *p = 1; *p = 2; }
int oa_reload(const int *p) { int x = *p; return x + *p; }
int oa_volatile(const volatile int *p) { return *p + *p; }
unsigned oa_cse(unsigned a, unsigned b) { return (a * b + 7) ^ ((a * b + 7) >> 3); }
unsigned oa_range(unsigned x) { if (x < 10) { if (x < 20) return 1; return 2; } return 3; }
extern int other(int);
int oa_tail(int x) { return other(x); }
unsigned oa_popcount(unsigned long x) { return __builtin_popcountl(x); }
int oa_switch(unsigned x) {
    switch (x) {
    case 0: return 17; case 1: return 3; case 2: return 92; case 3: return 8;
    case 4: return 41; case 5: return 6; case 6: return 29; case 7: return 55;
    default: return -1;
    }
}
void oa_vector_add(vec *d, const vec *a, const vec *b) { *d = *a + *b; }
unsigned oa_sum(const unsigned *a, unsigned n) {
    unsigned s = 0;
    for (unsigned i = 0; i < n; i++) s += a[i];
    return s;
}
unsigned oa_licm(const unsigned *a, unsigned n, unsigned x, unsigned y) {
    unsigned s = 0;
    for (unsigned i = 0; i < n; i++) s += a[i] / (x * y + 1u);
    return s;
}
void oa_copy(unsigned *restrict d, const unsigned *restrict s, unsigned n) {
    for (unsigned i = 0; i < n; i++) d[i] = s[i];
}
unsigned oa_dead_local(unsigned x) { unsigned unused = x * 29u; return x + 1; }
unsigned oa_aggregate(unsigned x, unsigned y) {
    struct pair { unsigned a, b; } p = { x, y };
    return p.a + p.b;
}
unsigned oa_cond_fold(unsigned x) { if (sizeof(x) == 4) return x + 1; return x + 2; }
