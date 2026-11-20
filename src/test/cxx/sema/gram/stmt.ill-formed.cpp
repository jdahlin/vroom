// Function bodies the rules of S1c find ill-formed (statements, block
// scope, jumps, switch, return, calls), one per line, and their near
// misses that are well-formed: bench/cxx-sema.py compares each verdict
// with clang's.
void v1() { return 1; }
int i1() { return; }
void v2() { return {}; }
void v3() { return v1(); }
int i2() { return v1(); }
void sw(int n) { switch (n) { case 1: int x = 1; case 2: break; } }
void sw2(int n) { switch (n) { case 1: { int x = 1; } case 2: break; } }
void g1() { goto L; int x = 1; L: ; }
void g2() { goto L; int x; L: ; }
void g3() { L: ; L: ; }
void g4() { goto M; }
void b1() { break; }
void c1() { switch (1) { continue; } }
void d1(int n) { switch (n) { default: default: ; } }
void d2(int n) { switch (n) { case 1: case 1: ; } }
void d3(char n) { switch (n) { case 300: ; } }
void d4(int n) { switch (n) { case 0x100000000: ; } }
void d5(int n) { switch (n) { case n: ; } }
void p1(int a) { int a; }
void p2(int a) { { int a; } }
void f1() { for (int i = 0;;) { int i; } }
void f2() { if (int i = 1) { int i; } else { int i; } }
int h(int, int);
void call1() { h(1); }
void call2() { h(1, 2, 3); }
void call3() { h(1, 2); int *p = 0; h(p, 2); }
void loc1() { int x; int x; }
void loc2() { typedef int T; typedef int T; typedef long T; }
void loc3() { inline int x = 1; }
void var(...);
void call4() { var(v1()); }
void call6() { var(1, 2.0); }
int i3(int &r) { return r; }
long l1(int a) { return a; }
int &lr(int &r) { int &q = r; return q; }
void n1() { int a[2] = {1, 2}; }
void n2() { char c = {300}; }
void s1() { switch (1.0) { } }
void w1() { while (int i = 0) { int i; } }
void cond1() { if (int f() = 0) ; }
void fp1() { void (*p)(int) = 0; p(1); p(); }
void ex1() { extern int e; }
void st1() { static int s = 1; static int s2 = s; }

// [ext:clang_vla]: a jump into the scope of a variable length array, bypassing
// its declaration, is ill-formed whether or not it is initialized.
void vla_goto(int n) { goto L; int v[n]; L: v[0] = 1; }
void vla_case(int n) { switch (n) { case 1: int v[n]; v[0] = 0; break; default: v[0] = 1; } }
void vla_ok_block(int n) { switch (n) { case 1: { int v[n]; v[0] = 0; } break; default: break; } }
void vla_ok_back(int n) { L: int v[n]; v[0] = 0; if (n--) goto L; }
void vla_ok_out(int n) { { int v[n]; v[0] = 0; goto out; } out:; }
void vla_ok_over_block(int n) { goto L; { int v[n]; v[0] = 0; } L:; }
