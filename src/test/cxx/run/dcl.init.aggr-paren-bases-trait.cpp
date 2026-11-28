/* [dcl.init.general]/16.6.2.2, [dcl.init.aggr]/2: __is_constructible of an
   aggregate with base classes from a parenthesized list: the bases are its
   first elements. */
struct Base {
    int a;
};
struct Other {
    int b;
};
struct Derived : Base, Other {
    long c;
};
struct Empty {};
struct Tagged : Empty {
    int n;
};

static_assert(__is_constructible(Derived, Base, Other, long));
static_assert(__is_constructible(Derived, Base, Other));
static_assert(__is_constructible(Derived, Base));
static_assert(!__is_constructible(Derived, Other));
static_assert(!__is_constructible(Derived, int, int));
static_assert(!__is_constructible(Derived, Base, Other, long, int));
static_assert(__is_constructible(Tagged, Empty, int));
static_assert(!__is_constructible(Tagged, int));
static_assert(__is_nothrow_constructible(Derived, Base, Other, long));

int main()
{
    return !__is_constructible(Derived, Base, Other);
}
