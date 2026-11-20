/* An alias-declaration that is not analysed declares the alias name only
   ([dcl.typedef]/2): intmax_t_, brought into namespace lib by a
   using-declaration and used in its type-id, stays the type it was, so
   the class template below is analysed.  The one error is the alias
   declaration, not the lookup of intmax_t_ in D<R<3>> (libc++'s
   <__atomic/aliases.h>, then <ratio>). */
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
