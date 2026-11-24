namespace st {
template<class T> struct rr { typedef T type; };
template<class T> T&& fwd(T& t) { return static_cast<T&&>(t); }
template<class T> T&& mv(T& t) { return static_cast<T&&>(t); }
}
struct F {
  template <class I>
  auto operator()(I& i) const noexcept(noexcept(st::mv(*st::fwd<I>(i)))) -> decltype(st::mv(*st::fwd<I>(i))) { return st::mv(*st::fwd<I>(i)); }
};
int f(){ int x = 1; int* p = &x; int*& q = p; return F{}(q); }
int main() { if (f() != 1) return 1; return 0; }
