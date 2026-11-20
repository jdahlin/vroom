/* [expr.prim.id.qual]/2, [temp.names]/4, [expr.unary.op]/4: the
   specialization a qualified template-id names: &S::template s<int>, a
   static one through a derived class's name, a pointer to a const member
   function template's specialization, one of a class template's
   specialization, and a call through such a pointer to member. */
struct S {
    template <class T> T f(T t) const { return t + 1; }
    template <class T> static T s(T t) { return t + 2; }
};
struct D : S {};
template <class U> struct C {
    template <class T> U g(T t) { return U(t) * 2; }
};
int main()
{
    int (*a)(int) = &S::template s<int>;
    int (*b)(int) = D::s<int>;
    long (S::*c)(long) const = &S::f<long>;
    using CI = C<int>;
    int (CI::*d)(char) = &C<int>::g<char>;
    S o;
    C<int> k;
    return a(1) + b(2) + int((o.*c)(3)) + (k.*d)(4) - (3 + 4 + 4 + 8);
}
