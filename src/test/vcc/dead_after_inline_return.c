/* statements after an inlined function's taken return are not generated (the kernel's bio_integrity_action: if (!blk_get_integrity(d)) return 0; ... return __bio_integrity_action(b)); act() is never defined */
extern unsigned act(void *b);
struct d { void *q; };
static inline void *gi(struct d *d) { return (void *)0; }
static inline int bi(void *b) { return b != 0; }
static inline unsigned ia(struct d *d, void *b) { if (!gi(d)) return 0; if (bi(b)) return 0; return act(b); }
unsigned f(struct d *d, void *b) { unsigned a = ia(d, b); return a + 1; }
int main(void) { struct d x = { 0 }; return f(&x, &x) != 1; }
