/* [class.default.ctor]/1: X(X&&) = default (and X(const X&) = default) is
   a user-declared constructor, so no default constructor is implicitly
   declared; /2: a class inheriting the constructors of such a base has a
   deleted default constructor.  libc++'s <variant>: __dtor and the classes
   over it (using __base_type::__base_type; defaulted copy and move
   constructors), Stockfish's score.h. */
struct base {
    int index;
    base(const base &) = default;
    base(base &&) = default;
    explicit base(int i) : index(i) {}
};
struct holder {
    int v;
    holder(const holder &) = default;
    holder(holder &&) = default;
};
static_assert(!__is_constructible(holder), "no default constructor");
static_assert(__is_constructible(holder, holder &&), "a move constructor");
struct derived : base {
    using base::base;
};
static_assert(!__is_constructible(derived), "the base has none");
static_assert(__is_constructible(derived, int), "an inherited one");
/* [class.copy.ctor]/14, [dcl.stc]/9: a mutable member is copied from a
   non-const lvalue, where [over.ics.rank]/3.2.6 prefers a constructor
   template X(T&) to X(const X&): not trivial; /8, /14: the implicit move
   constructor moves it from an xvalue, which X(T&) cannot bind: trivial
   (clang's CXX/special/class.copy/p12-0x.cpp) */
struct template_ctor {
    template <class T> template_ctor(T &);
};
struct mutable_member {
    mutable_member(const mutable_member &) = default;
    mutable_member(mutable_member &&) = default;
    mutable template_ctor m;
};
static_assert(!__is_trivially_constructible(mutable_member, const mutable_member &), "copy");
static_assert(__is_trivially_constructible(mutable_member, mutable_member &&), "move");
struct implicit_only {
    mutable template_ctor m;
};
static_assert(!__is_trivially_constructible(implicit_only, const implicit_only &), "implicit copy");
static_assert(__is_trivially_constructible(implicit_only, implicit_only &&), "implicit move");
int main()
{
    derived d(4);
    derived e(static_cast<derived &&>(d));
    if (e.index != 4)
        return 1;
    base b(e);
    if (b.index != 4)
        return 2;
    return 0;
}
