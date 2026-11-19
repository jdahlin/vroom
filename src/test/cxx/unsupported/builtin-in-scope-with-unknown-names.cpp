/* A name spelled __builtin_ is the implementation's ([lex.name]/3): a
   namespace definition that is not analysed (here A's second, in an
   extern "C++" block with a declaration the parser kept as tokens) leaves the
   names of A not known, but not the builtin that A's function template g
   calls when it is instantiated.  The one error is the declaration kept as
   tokens, not the lookup of __builtin_fabs (libc++'s __math::fabs after
   glibc's <math.h>). */
namespace A {
template <class T>
double g(T x)
{
    return __builtin_fabs(x);
}
}
extern "C++" {
namespace A {
typedef _Float16 half_t;
}
}
namespace A {
double z = g(1.5);
}
