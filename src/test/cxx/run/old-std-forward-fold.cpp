// std::forward, std::move and std::as_const of an lvalue: the object itself, no function called (clang's builtins)
namespace std {
template <class T> struct remove_reference { typedef T type; };
template <class T> struct remove_reference<T &> { typedef T type; };
template <class T> T &&forward(typename remove_reference<T>::type &t) noexcept { return static_cast<T &&>(t); }
template <class T> typename remove_reference<T>::type &&move(T &&t) noexcept { return static_cast<typename remove_reference<T>::type &&>(t); }
template <class T> const T &as_const(T &t) noexcept { return t; }
}
struct P { int v; };
static int take(int &x) { return x; }
static int take(int &&x) { return x + 100; }
template <class T> int fwd(T &&t) { return take(std::forward<T>(t)); }
int main() {
  int i = 1; P p{5};
  int a = fwd(i), b = fwd(2), c = take(std::move(i)), d = std::as_const(p).v;
  std::forward<int &>(i) = 7;
  return a == 1 && b == 102 && c == 101 && d == 5 && i == 7 ? 0 : 1;
}
