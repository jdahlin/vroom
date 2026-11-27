/*
 * [expr.static.cast]/2, /4 and [expr.const.cast]/4: static_cast and
 * const_cast to a non-const (or volatile) lvalue reference from an xvalue
 * are ill-formed, so a requires-expression testing them evaluates to false.
 */

template <class T>
T &&declval() noexcept;

template <class From, class To>
concept can_static_cast = requires { static_cast<To>(declval<From>()); };

template <class From, class To>
concept can_const_cast = requires { const_cast<To>(declval<From>()); };

struct Box {
    int v;
    operator int &() { return v; }
};

static_assert(!can_static_cast<int, int &>);
static_assert(!can_static_cast<int, volatile int &>);
static_assert(!can_static_cast<int, const volatile int &>);
static_assert(can_static_cast<int, const int &>);
static_assert(can_static_cast<int &, int &>);
static_assert(can_static_cast<Box, int &>);

static_assert(!can_const_cast<const int, int &>);
static_assert(can_const_cast<const int &, int &>);
static_assert(can_const_cast<const int, int &&>);

static Box make_box(int x) { return Box{x}; }

int main()
{
    int r = static_cast<int &>(make_box(42));
    if (r != 42)
        return 1;
    if (can_static_cast<int, int &>)
        return 2;
    if (!can_static_cast<int, const int &>)
        return 3;
    if (can_const_cast<const int, int &>)
        return 4;
    if (!can_const_cast<const int &, int &>)
        return 5;
    return 0;
}
