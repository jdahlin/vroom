// a function template declared in a namespace whose inline namespace declares the name joins one
// overload set with the namespace's functions and the inline namespace's templates and functions
// ([namespace.def]/7; libc++: swap(exception_ptr&, exception_ptr&) in std, the swap templates in
// std::__1, then json's swap(basic_json&, basic_json&) template in std)
namespace s {
inline namespace v1 {
template <class T> int sw(T &, T &) { return 1; }
template <class T, int N> int sw(T (&)[N], T (&)[N]) { return 2; }
int sw(long &, long &) { return 5; }
}
struct E {};
int sw(E &, E &) { return 4; }
}
namespace n { struct J {}; }
namespace s {
template <class T> int sw(n::J &, T &) { return 3; }
template <class T> int sw(n::J &, T *) { return 6; }
}
namespace s {
inline namespace v1 {
int sw(short &, short &) { return 7; }
}
}
int f() {
  using s::sw;
  int a = 0, b = 0, x[2], y[2];
  long l = 0;
  short h = 0;
  n::J j;
  s::E e;
  return sw(a, b) == 1 && sw(x, y) == 2 && s::sw(j, a) == 3 && sw(e, e) == 4 && s::sw(e, e) == 4 &&
         sw(l, l) == 5 && s::sw(l, l) == 5 && sw(j, &a) == 6 && sw(h, h) == 7 && s::sw(a, b) == 1;
}
int main() { return f() ? 0 : 1; }
