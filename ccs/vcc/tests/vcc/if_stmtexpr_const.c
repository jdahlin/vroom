/* if (WARN(!0)) return: the statement expression's constant value decides the if, the call after it is dead (the kernel's WARN_ON_ONCE(!IS_ENABLED(...))) */
extern void never(void);
#define WARN(c) ({ int r_ = !!(c); if (__builtin_expect(r_, 0)) __asm__ volatile("nop"); __builtin_expect(r_, 0); })
#define en 0
int *f(void) { if (WARN(!en)) return 0; never(); return 0; }
int main(void) { return f() != 0; }
