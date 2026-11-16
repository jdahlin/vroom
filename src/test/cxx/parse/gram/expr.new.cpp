// [expr.new], [expr.delete].
struct S {
  S();
  S(int, int);
};
typedef int I;
void *operator new(decltype(sizeof 0), void *) noexcept;
void *operator new[](decltype(sizeof 0), void *) noexcept;
void *operator new(decltype(sizeof 0), int, int);
extern void *buffer;
extern int n;

int *p1 = new int;
int *p2 = new int(3);
int *p3 = new int{3};
int *p4 = new int[n];
int *p5 = new int[n + 1]{1, 2};
int **p6 = new int *;
int **p7 = new int *[n];
int (*p8)[3] = new int[n][3];
int (**p9)() = new (int (*)());
const int *p10 = new const int(1);
S *p11 = new S;
S *p12 = new S(1, 2);
S *p13 = new S{1, 2};
S *p14 = new (buffer) S;
S *p15 = new (buffer) S(1, 2);
S *p16 = new (1, 2) S{};
int *p17 = new (buffer) (int);
int *p18 = new (int)(4);
int *p19 = ::new int;
int *p20 = ::new (buffer) int[n];
I *p21 = new I(1);
I *p22 = new (I);
unsigned long *p23 = new unsigned long;
int *const *p24 = new int *const(nullptr);
int S::**p25 = new int S::*;

int r1 = (delete p1, 0);
int r2 = (delete[] p4, 0);
int r3 = (::delete p19, 0);
int r4 = (::delete[] p20, 0);
int r5 = (delete (int *)p3, delete[] (p5), 0);
