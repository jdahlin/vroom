/* [temp.inst]/5: constant evaluation instantiates a constexpr member of a
   class template defined outside it (libc++'s __bitset). */
extern "C" int printf(const char *, ...);
template <int N> struct B { constexpr B(int v); constexpr int get(int v) const; constexpr explicit operator int() const; int first; };
template <int N> constexpr B<N>::B(int v) : first(v) {}
template <int N> constexpr int B<N>::get(int v) const { return v + first + N; }
template <int N> constexpr B<N>::operator int() const { return first; }
constexpr B<16> b(7); static_assert(b.first == 7 && b.get(1) == 24 && int(b) == 7, "");
template <int W, int N> struct P;
template <int N> struct P<1, N> { constexpr P(unsigned long long v); unsigned long long first; };
template <int N> constexpr P<1, N>::P(unsigned long long v) : first(v) {}
int main(){ constexpr P<1, 16> p(7); static_assert(p.first == 7, ""); static_assert(B<2>(1).get(1) == 4, ""); B<2> r(3); printf("%d %d %llu %d\n", b.get(1), r.get(1), p.first, int(r)); }
