// std: c++20
// alignas/aligned on classes and members laid out as clang does (an empty class too, and one with empty
// aligned bases: libc++'s aligned_storage); a partial specialization on R(A...) with members using the
// pack (std::function's __value_func); T{} value-initializes rather than taking an initializer_list
namespace std {
template <class E> class initializer_list {
  const E *__begin_;
  decltype(sizeof 0) __size_;
public:
  constexpr initializer_list() noexcept : __begin_(nullptr), __size_(0) {}
  constexpr decltype(sizeof 0) size() const noexcept { return __size_; }
};
}
struct alignas(16) A { char c; };
struct B { alignas(8) char c; int x; };
struct C { __attribute__((aligned(32))) char c; };
template <class T> struct alignas(__alignof(T)) E {};
template <class... T> struct M : E<T>... {};
template <unsigned N, unsigned Al = alignof(M<double, long double>)> union alignas(Al) S { unsigned char d[N]; };
template <class F> struct VF;
template <class F> struct Base;
template <class R, class... As> struct VF<R(As...)> { S<24> buf; typedef Base<R(As...)> fn; fn *f; };
struct L { int n = -1; L() : n(0) {} L(std::initializer_list<int> l) : n((int)l.size() + 10) {} };
struct H { L l{}; };
int main() {
  H h;
  return sizeof(A) == 16 && alignof(B) == 8 && sizeof(B) == 8 && sizeof(C) == 32 && alignof(E<double>) == 8 &&
         sizeof(E<double>) == 8 && alignof(M<double, long double>) == 16 && sizeof(S<24>) == 32 &&
         sizeof(VF<int(int, long)>) == 48 && h.l.n == 0 ? 0 : 1;
}
