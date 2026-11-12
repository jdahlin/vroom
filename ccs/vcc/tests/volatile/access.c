/* Volatile accesses are kept one for one: bench/vcc-volatile.py counts the loads and
   stores in each function's -S (callee-save traffic aside) and compares them with
   the "ld N st N" its comment gives, for the direct emitter and for VIR.  Counts
   are clang's at -O2 (a bit-field store reads its unit first). */
void esc(volatile int *);
struct B { volatile unsigned a : 3; int z; };
struct B bf;
volatile int g;
volatile unsigned char gb[4];

/* ld 2 st 0 */ int local_twice(void) { volatile int x; esc(&x); int a = x; int b = x; return a + b; }
/* ld 1 st 1 */ int local_store_load(void) { volatile int x; esc(&x); x = 1; return x; }
/* ld 0 st 2 */ void local_dead_stores(void) { volatile int x; x = 1; x = 2; }
/* ld 3 st 1 */ int local_discarded(void) { volatile int x = 1; x; x; return x; }
/* ld 0 st 1 */ void local_init(void) { volatile int x = 5; }
/* ld 0 st 3 */ void local_init_rec(void) { volatile struct { int a, b; } s = { 1, 2 }; }
/* ld 2 st 0 */ int bf_twice(void) { return bf.a + bf.a; }
/* ld 2 st 2 */ void bf_stores(void) { bf.a = 1; bf.a = 2; }
/* ld 4 st 2 */ void bf_update(void) { bf.a++; bf.a += 2; }
/* ld 3 st 3 */ void global_update(void) { g++; g += 2; g |= 1; }
/* ld 2 st 0 */ int ptr_twice(volatile int *p) { return *p + *p; }
/* ld 0 st 2 */ void ptr_stores(volatile int *p) { *p = 0; *p = 0; }
/* ld 4 st 0 */ unsigned bytes(void) { return gb[0] | gb[1] << 8 | gb[2] << 16 | (unsigned)gb[3] << 24; }
/* ld 4 st 0 */ unsigned pbytes(volatile unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
/* ld 2 st 0 */ int index_twice(volatile int *p, long i) { return p[i] + p[i]; }
/* ld 1 st 0 */ void discarded(volatile int *p) { *p; }
