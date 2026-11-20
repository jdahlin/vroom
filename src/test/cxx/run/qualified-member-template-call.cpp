/* [expr.prim.id.qual]/2, [temp.names]/4: a call through a class-qualified
   name of a static member function template, with and without the
   keyword template, explicit template arguments or deduction (libc++'s
   pair: _CheckArgsDep::template __is_pair_constructible<...>()) */
struct check {
    template <class T> static constexpr bool ok() { return sizeof(T) == 4; }
    template <class T> static constexpr T twice(T t) { return t + t; }
    template <class T, class U> static constexpr U pick(U u) { return u; }
};

template <class C = check, bool = C::template ok<int>()>
struct enabled {
    static constexpr int value = 1;
};

template <class C> constexpr int through() {
    return C::template twice<int>(3) + C::template ok<char>() + C::twice(4);
}

static_assert(check::ok<int>() && !check::ok<char>(), "");
static_assert(check::template ok<int>(), "");
static_assert(check::twice(5) == 10 && check::twice<long>(5) == 10, "");
static_assert(check::pick<int>('a') == 'a', "");
static_assert(enabled<>::value == 1, "");
static_assert(through<check>() == 14, "");

int main()
{
    return through<check>() - 14 + check::twice(2) - 4 + enabled<>::value - 1;
}
