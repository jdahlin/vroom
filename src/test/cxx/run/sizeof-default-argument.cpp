/* [expr.sizeof]/1: the operand is unevaluated, so a non-static data member
   may be named in it ([expr.prim.id]/3) in a default argument
   ([dcl.fct.default]/9: "A non-static member shall not appear in a default
   argument unless it appears as the id-expression of a class member access
   expression or unless it is used to form a pointer to member"
   ([expr.prim.id]/3's unevaluated operands are no use of the member)) */
struct S {
    int m;
    char buf[10];
    unsigned long f(unsigned long n = sizeof(m)) const { return n; }
    unsigned long g(unsigned long n = sizeof(buf) + alignof(S)) const { return n; }
    unsigned long h(unsigned long n = sizeof(this->m)) const { return n; }
};
int main()
{
    S s{};
    return s.f() != 4 || s.g() != 14 || s.h() != 4 || s.f(2) != 2;
}
