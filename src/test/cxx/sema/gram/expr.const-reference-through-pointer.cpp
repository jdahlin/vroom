// [expr.const]/13.1: a constexpr reference bound to an element through a
// pointer designates the pointee, an object of static storage duration; the
// local pointer variable it is read from does not matter.
void f() {
  static int g;
  constexpr int *p = &g;
  constexpr int &r = p[0];
}
