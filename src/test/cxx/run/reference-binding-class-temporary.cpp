/* [dcl.init.ref]/5, [class.temporary]/4 and /6, [conv.rval]: a reference
   bound to a class xvalue (std::move's static_cast) names the object and
   makes no temporary; a prvalue of a class with a destructor is
   materialized, destroyed at the end of the full-expression, or, bound to
   a declared reference, as the reference's block ends (in reverse order
   of construction, through a base subobject too) */
extern "C" int printf(const char *, ...);
struct B {
    int b;
    B() : b(1) {}
    B(int x) : b(x) {}
    ~B() { printf("~B%d\n", b); }
};
struct D : B {
    D() : B(9) {}
    ~D() { printf("~D\n"); }
};
template <class T> struct plain { using type = T; };
template <class T> struct plain<T &> { using type = T; };
template <class T> struct plain<T &&> { using type = T; };
template <class T> typename plain<T>::type &&move(T &&t) noexcept
{
    return static_cast<typename plain<T>::type &&>(t);
}
B make() { return B(5); }
int take(const B &x) { return x.b; }
int steal(B &&x) { return x.b + 1; }
int main()
{
    D d;
    const D cd;
    D &&a = move(d);
    B &&b = move(d);
    const D &&c = move(cd);
    printf("%d %d %d %d\n", a.b, b.b, c.b, steal(move(d)));
    {
        const B &r = make();
        B &&q = B(7);
        const B &s = D();
        printf("%d %d %d\n", r.b, q.b, s.b);
        printf("%d\n", take(B(3)) + steal(B(4)));
        printf("end\n");
    }
    printf("%d\n", take(make()));
    return 0;
}
