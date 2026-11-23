/* A function template's trailing requires-clause ([dcl.decl]/4), checked
   at deduction ([temp.constr.decl]/3): namespace-scope templates and a
   member operator() taking a reference to an array of unknown bound, as
   libc++'s ranges::begin (__ranges/access.h). */
template <class T>
constexpr int pick(T) noexcept
    requires(sizeof(T) == 1)
{
    return 1;
}

template <class T>
constexpr int pick(T) noexcept
    requires(sizeof(T) > 1)
{
    return 2;
}

struct fn {
    template <class T>
    [[nodiscard]] constexpr auto operator()(T (&t)[]) const noexcept
        requires(sizeof(T) >= 0)
    {
        return t[1];
    }
};

int a[2] = {3, 4};

static_assert(pick('x') == 1);
static_assert(pick(1L) == 2);

int main()
{
    fn f;
    int(*p)[] = (int(*)[])&a;
    volatile char c = 0;
    volatile int i = 0;
    if (pick(c) != 1 || pick(i) != 2)
        return 1;
    return f(*p) == 4 ? 0 : 2;
}
