/* [dcl.stc]/8 in a class template, with a using-declaration of a typedef-name
   in its type-id (libc++'s <__atomic/aliases.h>, then <ratio>) */
typedef long intmax_t_;
namespace lib {
using ::intmax_t_;
}
namespace lib {
template <class T>
struct holder {
    mutable T value;
};
using wide = holder<intmax_t_>;
template <intmax_t_ N>
struct R {
    static constexpr intmax_t_ n = N;
};
template <class A>
struct D {
    static const intmax_t_ g = A::n;
};
typedef D<R<3> > X;
}

int main()
{
    const lib::wide w = {5};
    w.value = 7;
    if (w.value != 7 || lib::X::g != 3)
        return 1;
    return 0;
}
