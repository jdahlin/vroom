/* [expr.const]/10, [conv.prom]/3-4: a converted constant expression of a
   non-type template-parameter of integral type may be an unscoped
   enumerator, promoted (fmt's conditional_t<long_short, int, long long>,
   long_short an enumerator of an unnamed enumeration). */
enum { two = 2, one = 1 };
enum E : short { c = 3 };
template <bool B, class T, class F> struct cond { typedef T type; };
template <class T, class F> struct cond<false, T, F> { typedef F type; };
template <int N> struct I { static const int v = N; };
cond<one, int, long>::type a = 1;
cond<(bool)0, int, long>::type b = 2;
I<two> i2; I<c> i3;
static_assert(sizeof(cond<one, char, long>::type) == 1, "");
int main() { return I<two>::v + I<c>::v - 5; }
