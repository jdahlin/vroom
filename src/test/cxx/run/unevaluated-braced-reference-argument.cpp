/* [dcl.init.ref]/5, [expr.context]/1: a call in an unevaluated operand at
   namespace scope whose reference parameter is initialized from {}
   (libc++'s __is_implicitly_default_constructible) needs no temporary. */
template <class T>
void test(T);

template <class T, class = void>
struct implicit_default {
    static constexpr bool value = false;
};

template <class T>
struct implicit_default<T, decltype(test<T const &>({}))> {
    static constexpr bool value = true;
};

struct explicit_only {
    explicit explicit_only() {}
};

static_assert(implicit_default<int>::value);
static_assert(!implicit_default<explicit_only>::value);

int main()
{
    int result = 1;
    if (implicit_default<int>::value)
        result = 0;
    return result;
}
