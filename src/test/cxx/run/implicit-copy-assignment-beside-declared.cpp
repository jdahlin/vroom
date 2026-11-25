// [class.copy.assign]/2, [over.match.viable]/4: the implicit copy assignment operator X& operator=(const
// X&) of a class that declares other assignment operators: no candidate for a right operand of
// another class that does not convert to X (libc++'s back_insert_iterator = value), and the
// trivial copy for one of X where no declared operator= is viable (ostreambuf_iterator = it)
template <class C> struct bii {
  C *c;
  explicit bii(C &x) : c(&x) {}
  bii &operator=(const typename C::value_type &v) { c->push(v); return *this; }
  bii &operator=(typename C::value_type &&v) { c->push(v); return *this; }
  bii &operator*() { return *this; }
};
struct str { int n; };
struct vec { typedef str value_type; int sum = 0; void push(const str &s) { sum += s.n; } };
template <class Ch> struct osi {
  int *p;
  osi &operator=(Ch c) { *p += c; return *this; }
};
int main() {
  vec v; bii<vec> it(v); str s{4}; *it = s; *it = str{5};
  int k = 0; osi<char> a{&k}, b{nullptr}; b = a; b = 'x' - 'w';
  return v.sum == 9 && k == 1 && b.p == &k ? 0 : 1;
}
