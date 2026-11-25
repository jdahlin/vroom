// c.end() where end is a member enumerator (libc++'s ios_base::seekdir::end) is a substitution failure
// in decltype(c.end()), not unsupported: json's is_range<std::ostream> asks std::end(ostream&)
struct S { enum E { end = 3 }; };
template <class C> auto f(C &c, int) -> decltype(c.end()) { return 7; }
template <class C> int f(C &, long) { return 1; }
int main() {
  S s;
  int a = s.end;
  return a == 3 && f(s, 0) == 1 ? 0 : 1;
}
