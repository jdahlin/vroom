/* [except.spec]/8: an inheriting constructor template's exception
   specification is the inherited constructor's and the other subobjects' */
struct base {
    template <class A> base(int, A) noexcept : v(1) {}
    template <class A> base(A *) : v(2) {}
    int v;
};
struct quiet : base {
    using base::base;
    int n = 3;
};
struct thrower {
    thrower() noexcept(false) {}
};
struct loud : base {
    using base::base;
    thrower t;
};
int main()
{
    int i = 0;
    if (!noexcept(quiet(0, 1)) || noexcept(quiet(&i)))
        return 1;
    if (noexcept(loud(0, 1)))
        return 2;
    quiet q(0, 1);
    loud l(&i);
    return q.v != 1 || q.n != 3 || l.v != 2;
}
