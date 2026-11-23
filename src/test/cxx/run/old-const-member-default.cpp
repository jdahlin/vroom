// a const non-static data member with a default initializer is no constant: the constructor's
// mem-initializer gives its value (json's lexer: const bool ignore_comments = false)
template <class T> T &&mv(T &t) { return static_cast<T &&>(t); }
struct A { const char *p; A(A &&o) : p(o.p) {} A(const char *s) : p(s) {} };
template <class X> struct L {
  explicit L(X &&a, bool ic = false) noexcept : ia(mv(a)), ic_(ic), dp('.') {}
  int scan() { return ic_ ? 15 : 14; }
  X ia;
  const bool ic_ = false;
  int cur = -1;
  const char dp = ',';
};
int f(bool b) { A a("x"); return L<A>(mv(a), b).scan(); }
int main() { return f(true) == 15 && f(false) == 14 ? 0 : 1; }
