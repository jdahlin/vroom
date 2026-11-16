// [stmt.ambig] Example 3 (paragraph 3), docs/cxx-spec/stmt/stmt.ambig.md
// needs: P5
// status: well-formed (as far as the example says)

struct T1 {
  T1 operator()(int x) { return T1(x); }
  int operator=(int x) { return x; }
  T1(int) { }
};
struct T2 { T2(int){ } };
int a, (*(*b)(T2))(int), c, d;

void f() {
  // disambiguation requires this to be parsed as a declaration:
  T1(a) = 3,
  T2(4),                        // T2 will be declared as a variable of type T1, but this will not
  (*(*b)(T2(c)))(int(d));       // allow the last part of the declaration to parse properly,
                                // since it depends on T2 being a type-name
}
