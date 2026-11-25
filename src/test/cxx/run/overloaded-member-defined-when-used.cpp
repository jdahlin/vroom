// [temp.inst]/4, [over.match]/3: the member function overload resolution chose, a static member
// of a class template's specialization declared after another of its name, is the one called and
// so instantiated (libc++'s basic_string::__allocate_long_buffer(alloc, size) calling its
// three-parameter overload, from __init_internal_buffer)
template <class C> struct str {
  static int alloc(int a, int b, int c) { return a + b + c; }
  static int alloc(int a, int b) { return alloc(a, b, 10); }
  int init(int n) { return alloc(n, n); }
};
int main() {
  str<char> s;
  return s.init(1) == 12 ? 0 : 1;
}
