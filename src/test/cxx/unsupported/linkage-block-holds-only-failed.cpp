/* [dcl.link]/1: the declarations of an extern "C++" block are the enclosing
   scope's, each analysed on its own.  One it cannot analyse (here a
   declaration the parser kept as tokens) holds its own names; the
   namespace definition beside it, which was analysed, stays known.  So
   `std` found from inside std is the namespace ([basic.lookup.qual]/1) and
   the error is the use of a member std::limits does not have, not the
   declaration kept as tokens (libc++'s __math::__hypot after glibc's
   <math.h>, whose extern "C++" block holds such a declaration). */
extern "C++" {
namespace std {
template <class T>
struct limits {
    static constexpr int digits = 3;
};
}
typedef _Float16 half_t;
}
namespace std {
template <class R>
int f(R)
{
    return std::limits<R>::digits;
}
int g = std::limits<int>::no_such_member;
}
