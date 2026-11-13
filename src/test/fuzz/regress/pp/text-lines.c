#define P()
#define L() P()
L()

typedef int x;
#define D(s) _Pragma(#s)
#define K() D(a); D(b)
#define H() K()
int x;
H();
  D(c)
int y;
#define T(x) x
T(1 +
2)
