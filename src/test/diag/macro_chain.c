/* D5.2: the "expanded from macro" chain through nested macros (clang's backtrace, the
   middle skipped past -fmacro-backtrace-limit), the caret at the invocation, operands in
   the body, system-header frames; vcc's text folds the middle frames and shows the
   expansion with the arguments put in */
/* diag-flags: -pedantic */
#include "macro_sys.h"
#define INNER(x) ((x) + undefined_name)
#define OUTER(y) INNER(y * 2)
#define ADD(a, b) ((a) + (b))
#define SE(x) ({ int _t = (x); _t; })
#define STR(x) #x
#define BAD_STR(x) (STR(x) + 1.5f * (struct S){0})
#define V(fmt, ...) vf(fmt, __VA_ARGS__ + nope2)
#define L1(x) L2(x)
#define L2(x) L3(x)
#define L3(x) L4(x)
#define L4(x) L5(x)
#define L5(x) L6(x)
#define L6(x) L7(x)
#define L7(x) ((x) + deep_undefined)
struct S { int v; };
int vf(const char *, ...);
int f(int k) { return OUTER(k); }
int g(struct S s) { return ADD(s, 1); }
int h(void) { return SE(1); }
int j(int k) { return V("%d", k, 2); }
int l(int k) { return L1(k); }
int m(int k) { return S_OUT(k); }
