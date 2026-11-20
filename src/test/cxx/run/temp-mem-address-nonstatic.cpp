/* [temp.mem]/1, [over.over], [expr.unary.op]/4: the address of a non-static member function template's specialization &S::f<int>, a pointer to member function called through .* and ->* */
struct S {
    template <class T> T f(T t) { return t + 1; }
    template <class T> static T s(T t) { return t + 2; }
};
int main()
{
    S o;
    S *p = &o;
    int (S::*pm)(int) = &S::f<int>;
    return (o.*pm)(1) - 2 + (p->*pm)(2) - 3;
}
