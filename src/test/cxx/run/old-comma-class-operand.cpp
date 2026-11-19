// a, b with b an object of class type: b itself, an lvalue (++cnt, ++it in a for's step: json's serializer)
struct It { int *p; It &operator++() { ++p; return *this; } };
int main() {
  int a[3] = {1, 2, 3}, s = 0, n;
  It i{a};
  for (n = 0; n < 2; ++n, ++i) s += *i.p;
  It j{a};
  (n, j).p = a + 2;
  ++n, ++j;
  return s == 3 && j.p == a + 3 && n == 3 ? 0 : 1;
}
