[[deprecated]] int a(void);
[[deprecated("m")]] int b;
int a(void);
int a(void) { return 1; }
[[nodiscard]] int n(void);
int __attribute__((warn_unused_result)) w(void);
enum E { X __attribute__((deprecated)), Y };
__attribute__((deprecated)) void old(void) { a(); }
struct [[deprecated]] Q { int q; };
struct Q *qq;
int (*fp)(void) = a;
int k(void) { int s = sizeof(b); n(); w(); ({ w(); }); return X + Y + s; }
static inline void i(void) { (void)b; }
