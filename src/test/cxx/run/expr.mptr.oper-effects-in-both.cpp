/* [expr.mptr.oper]/4: the object expression is sequenced before the
   pointer to member, both with effects */
extern "C" int printf(const char *, ...);
struct A { int x, y; };
A a{1, 2};
A &obj() { printf("obj "); return a; }
int A::*mem() { printf("mem "); return &A::y; }
int main() { obj().*mem() += 3; int v = obj().*mem(); printf("%d\n", v); return v - 5; }
