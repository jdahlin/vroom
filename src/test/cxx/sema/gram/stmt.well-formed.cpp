// Function bodies that are well-formed, with expressions whose types and
// constants bench/cxx-sema.py has clang verify.
int add(int a, int b) { return a + b; }
void g(int n) {
  int x = 1, y;
  static int s;
  ;
  x = add(x, 2);
  if (x) y = 1; else { y = 2; }
  if (int z = x) y = z;
  while (x < 10) ++x;
  do { --x; } while (x);
  for (int i = 0; i < n; i++) { if (i == 3) continue; if (i > 5) break; }
  for (;;) break;
  switch (n) { case 1: x = 2; break; case 2: default: ; }
  L: goto L;
  { int x = 3; }
  return;
}
unsigned u(unsigned a, unsigned char c) { return a + c; }
double d(float f, long n) { return f * n; }
int nest(int a) { { int b = a; { int c = b + 1; return c; } } }
int tern(int a) { return a ? add(a, 1) : -a; }
void ptr(int *p) { int *q = p; *q = 1; q[0] = 2; }
int (*pick())(int, int) { return add; }
int call5() { return pick()(1, 2) + (*pick())(3, 4); }
// [expr.throw]: void prvalues, with and without an operand, also dependent
void thrower(int a) { if (a) throw a + 1; else throw; }
template <class T> void throw_dependent(T t) { throw t; }
