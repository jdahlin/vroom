/* if (WARN_ON_ONCE(!0)): the kernel's shape, !!(r_) of a bound constant inside the statement expression folds too */
extern void never(void);
int *f(void) { if (({ int r_ = !!(!0); if (__builtin_expect(!!(r_), 0)) do { __asm__ volatile(""); } while (0); __builtin_expect(!!(r_), 0); })) return 0; never(); return 0; }
int main(void) { return f() != 0; }
