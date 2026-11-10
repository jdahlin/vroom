__attribute__((deprecated)) int f(void);
__attribute__((deprecated("use g2"))) int g;
struct __attribute__((deprecated)) S { int x; };
typedef int T __attribute__((deprecated));
__attribute__((warn_unused_result)) int w(void);
int h(void) { struct S *p = 0; T t = 0; w(); (void)w(); return f() + g + t + (p != 0); }
