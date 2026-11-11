/* Block-local value reuse: correctness constraints for a future CSE pass. */
static volatile unsigned observed;
static unsigned global_value;
static unsigned update(void) { global_value++; return 7; }
unsigned lvr_square(unsigned a, unsigned b) { return (a + b) * (a + b); }
unsigned lvr_reuse(unsigned a, unsigned b) { unsigned x = a + b; return x * (a + b); }
unsigned lvr_commute(unsigned a, unsigned b) { return (a + b) ^ (b + a); }
unsigned lvr_kill(unsigned a, unsigned b) { unsigned x = a + b; a++; return x + (a + b); }
unsigned lvr_call(void) { unsigned x = global_value + 3; unsigned y = update(); return x + y + (global_value + 3); }
unsigned lvr_volatile(void) { return (observed + 1) * (observed + 1); }
unsigned lvr_alias(unsigned *p, unsigned *q) { unsigned x = *p + 2; *q = 7; return x + (*p + 2); }
unsigned lvr_join(unsigned a, unsigned b, int c) { unsigned x = a + b; if (c) a++; return x + a + b; }
int main(void)
{
 unsigned v = 3, w = 5;
 if (lvr_square(2, 3) != 25 || lvr_reuse(2, 3) != 25 || lvr_commute(3, 4) ||
     lvr_kill(2, 3) != 11 || lvr_join(2, 3, 0) != 10 || lvr_join(2, 3, 1) != 11) return 1;
 global_value = 4;
 if (lvr_call() != 22 || global_value != 5) return 2;
 observed = 3;
 if (lvr_volatile() != 16) return 3;
 if (lvr_alias(&v, &v) != 14 || v != 7 || lvr_alias(&v, &w) != 18 || w != 7) return 4;
 return 0;
}
