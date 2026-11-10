/* D1.0: messages name the type, as clang's do */
struct X;
struct S { int a; } s;
union U { int a; float b; };
int a[1.5];
_Atomic(int[2]) at;
_Atomic(struct X) *ax;
void f1(void) { struct X x; }
int f2(struct X *p) { return p->a; }
int g(void);
int *f3(void) { return &g(); }
int x = _Generic(1.0, int: 1);
int f4(void) { return -s; }
union U f5(double d) { return (union U)d; }
void f6(double d) { switch (d) { } }
