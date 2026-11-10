/* D8.1: -Wreturn-stack-address (clang's text and locations), a note at the
   declaration and a help on the lifetime; &register is an error */
struct S { int f; char a[4]; };
int *f1(void) { int x = 1; return &x; }
char *f2(void) { char buf[8]; return buf + 1; }
int *f3(int p) { return &p; }
int *f4(int c) { int x, y; return c ? &x : &y; }
char *f5(void) { struct S s; return s.a; }
long f6(void) { int x; return (long)&x; }
int *f7(void) { return (int[]){1, 2}; }
char *f8(void) { char b[4][4]; return b[1]; }
int *f9(void) { int x; return &*&x; }
int *ok1(void) { static int x; return &x; }
int *ok2(int *p) { return &p[1]; }
const char *ok3(void) { return "lit"; }
int *bad(void) { register int x; return &x; }
