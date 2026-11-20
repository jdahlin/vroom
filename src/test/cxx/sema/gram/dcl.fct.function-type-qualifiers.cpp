// [dcl.fct]/6: a function type with a cv-qualifier-seq or ref-qualifier
// appears only as a member function's, as the type a pointer to member
// refers to, as a typedef or alias's; one per line (ill-formed first, then
// the well-formed near misses), each verdict compared with clang's.
struct S { int f() const; };
int g1() const;
int (*p1)() const;
int (&r1)() const = *p1;
typedef int F() const;
F x1;
F *p2;
void g2(F);
void g3(int () const);
void g4(int (*)() const);
auto g5() -> int () const;
int (S::*q1)() const;
int (S::*q2)() const volatile &;
int (S::*q3)() &&;
F S::*q4;
using G = int(int) volatile;
G S::*q5;
void g6(int (S::*)() const);
int (S::*g7())() const;
int (S::*q6[2])() const;
struct T { int (S::*m)() const; };
