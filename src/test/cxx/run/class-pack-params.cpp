// std: c++20
// a member function of a class template instance whose parameters expand the class's pack:
// R operator()(As... a) (std::function's), and const As&... / As&&... declarators
template <class F> struct Fn;
template <class R, class... As> struct Fn<R(As...)> {
  R (*p)(As...);
  R operator()(As... a) const { return p(a...); }
  R cref(const As &...a) const { return p(a...); }
  R fwd(As &&...a) const { return p(static_cast<As &&>(a)...); }
};
enum class ev { a, b };
int mul(int x, int y) { return x * y; }
bool cb(int d, ev e, int &v) { v += d; return e == ev::b; }
int main() {
  Fn<bool(int, ev, int &)> f{cb};
  int v = 1;
  bool r = f(2, ev::b, v);
  Fn<int(int, int)> g{mul};
  int x = 3, y = 4;
  return r && v == 3 && g.cref(x, y) == 12 && g.fwd(5, 6) == 30 ? 0 : 1;
}
