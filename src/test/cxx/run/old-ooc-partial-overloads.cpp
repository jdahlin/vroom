// out-of-class constructors of a partial specialization (vector<bool>'s), one parameter each, some with
// an attribute before the name (_LIBCPP_HIDE_FROM_ABI): each definition goes to its own declaration
template <class T, class A> struct V { int n; };
template <class A> struct V<bool, A> {
  int n;
  explicit V(const A &a);
  V(const V &v);
  V(unsigned long m);
};
struct Al { int x; };
template <class A> __attribute__((__visibility__("hidden"))) inline V<bool, A>::V(const A &a) : n(a.x) {}
template <class A> V<bool, A>::V(const V &v) : n(v.n + 100) {}
template <class A> __attribute__((__visibility__("hidden"))) V<bool, A>::V(unsigned long m) : n((int)m * 2) {}
int main() {
  Al al{3};
  V<bool, Al> a(al), b(a), c(5ul);
  return a.n == 3 && b.n == 103 && c.n == 10 ? 0 : 1;
}
