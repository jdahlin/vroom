/* -Wformat: typedef names through casts, calls, indexing, members, parentheses, arithmetic and ?: */
int printf(const char *, ...);
typedef long myl; typedef unsigned long usz;
struct S { myl m; } s, *ps;
myl fn(void); myl arr[2];
void f(int i, myl v){ printf("%d %d %d %d %d %d %d\n", (myl)i, fn(), arr[1], ps->m, (v), v + 1, i ? v : v); }
