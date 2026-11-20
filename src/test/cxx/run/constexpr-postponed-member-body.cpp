/* [expr.const]/11, [temp.inst]/4, [class.prop]: a constexpr member function of a
   class template specialization named as a base is evaluated where a
   constant needs it, though its body waits for the end of the translation
   unit; an object of a class with empty bases is a constant value */
#include <stdio.h>

template <class T> struct limits_base {
protected:
    typedef T type;
    static constexpr const bool is_signed = type(-1) < type(0);
    static constexpr const type lo = is_signed ? T(T(1) << 31) : 0;
    static constexpr const type hi = is_signed ? type(type(~0) ^ lo) : type(~0);
    static constexpr type max() noexcept { return hi; }
};
template <class T> struct limits : private limits_base<T> {
    typedef limits_base<T> base;
    typedef typename base::type type;
    static constexpr type max() noexcept { return base::max(); }
};
constexpr unsigned long extent = limits<unsigned long>::max();
static_assert(extent == ~0ul);
static_assert(limits<int>::max() == 2147483647);

template <class D> struct adaptor {};
struct fn : adaptor<fn> {
    constexpr int operator()(int a) const { return a + 1; }
};
inline constexpr auto all = fn{};
static_assert(all(2) == 3);

struct b {
    int x;
    constexpr int getx() const { return x; }
};
struct d : b {
    int y;
    constexpr int sum() const { return x + y + getx(); }
};
constexpr d k = d{{1}, 2};
static_assert(k.sum() == 4);

int main()
{
    printf("%lu %d %d\n", extent, all(2), k.sum());
    return 0;
}
