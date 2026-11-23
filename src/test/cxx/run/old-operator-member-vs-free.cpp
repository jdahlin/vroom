// os << " " with a member operator<<(const void *) and a namespace-scope operator<<(OS<C>&, const C *) (a
// template, or not): the non-member converts the operand better and wins, as in one candidate set
namespace n {
template <class C> struct OS {
  int k = 0;
  OS &operator<<(int) { k = 1; return *this; }
  OS &operator<<(const void *) { k = 2; return *this; }
};
template <class C> OS<C> &operator<<(OS<C> &o, const C *) { o.k = 3; return o; }
struct P { int k = 0; P &operator<<(const void *) { k = 2; return *this; } };
P &operator<<(P &o, const char *) { o.k = 3; return o; }
}
int main() {
  n::OS<char> o; n::P p; int x = 0;
  o << " "; int a = o.k; o << 5; int b = o.k; o << (const void *)&x; int d = o.k;
  p << "s"; int e = p.k;
  return a == 3 && b == 1 && d == 2 && e == 3 ? 0 : 1;
}
