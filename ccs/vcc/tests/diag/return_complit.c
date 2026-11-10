struct S { int a, b; };
int *f1(void) { return (int[]){1, 2}; }
struct S *f2(int x) { return &(struct S){x, 2}; }
int *f3(void) { return (int[3]){0}; }
const char *f4(void) { return (char[]){"hi"}; }
int *f5(void) { static int *p; p = (int[]){1}; return p; }
int *f6(int y) { return (int[]){ y+1,2 ,3 }; }
int *f7(void) { int *p = (int[]){1}; return p; }
struct T { struct S s; int c[2]; };
int g(int);
struct S *h1(int x) { return &(struct S){ .a = x, .b = -x }; }
struct T *h2(void) { return &(struct T){ { 1, 2 }, { 3 } }; }
int *h3(int x) { return (int[]){ [1] = x * 2, (int)'c', g(x), !x, x << 1 }; }
long *h4(long x) { return (long[]){ x ? 1 : 2, ~x, x&&1 }; }
