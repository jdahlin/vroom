// an overloaded operator, (doctest's MessageBuilder: mb * "x := ", x) is called, not the built-in comma
// (which vcc used silently before); explicit calls through ->: this->operator,(in), p->operator+(4)
struct MB {
  int n = 0;
  template <class T> MB &operator,(const T &in) { n += (int)sizeof(in); return *this; }
  template <class T> MB &operator*(const T &in) { return this->operator,(in); }
  MB &operator+(int k) { n += k; return *this; }
};
int main() {
  MB m; char f[3] = "ab"; int i = 0;
  m * "x := ";	// 6
  m, f;		// 9
  m * "x := ", f;	// 18
  MB *p = &m; p->operator+(4);	// 22
  int b = (i = 1, i + 1);	// the built-in one with scalars
  return m.n == 22 && b == 2 ? 0 : 1;
}
// (m, f as the first use of operator,<char[3]>: the right operand's value category is its own, not what an
// earlier operator left: an lvalue array binds const T&)
struct MB2 { int n = 0; template <class T> MB2 &operator,(const T &in) { n += (int)sizeof(in); return *this; } };
static int first_use() { MB2 m; char f[3] = "ab"; m, f; return m.n; }
static int check_first_use = first_use() == 3 ? 0 : (__builtin_trap(), 1);
