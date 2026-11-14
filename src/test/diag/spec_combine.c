/* type specifiers that do not combine: clang's messages, then on as the first */
unsigned void a;
signed struct S { int x; } b;
long struct T { int x; } c;
signed float d;
long char e;
short double f;
long short g;
short long h;
long long char i;
unsigned _Bool j;
long float k;
int float l;
char int m;
static extern int n;
int fn1(int a, void);
int fn2(void x);
int fn3(void void);
int use(void) { return fn3() + b.x; }
