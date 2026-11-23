// static_cast down a class hierarchy (a reference and a pointer, the base not the first: an offset; a null
// pointer stays null) and up by pointer; a static data member of an explicit specialization named through a
// template parameter (libc++'s use_facet: static_cast<const ctype<char>&>(*l.use_facet(ctype<char>::id)))
struct F { virtual ~F() {} int a = 1; };
struct M { int m = 2; };
struct D : F, M { int d = 3; };
struct Id { int n; };
template <class T> struct C;
template <> struct C<char> : F, M { static Id id; int d = 4; };
Id C<char>::id{5};
template <class T> const T &facet(const M &m) { return static_cast<const T &>(m); }
template <class T> int idn() { return T::id.n; }
int main() {
  D d;
  M *pm = &d, *z = nullptr;
  const M &rm = d;
  C<char> cc;
  int r = static_cast<D *>(pm) == &d && static_cast<D *>(z) == nullptr && static_cast<M *>(&d) == pm &&
          &static_cast<const D &>(rm) == &d && static_cast<const D &>(rm).d == 3 &&
          facet<C<char>>(cc).d == 4 && idn<C<char>>() == 5;
  return r ? 0 : 1;
}
