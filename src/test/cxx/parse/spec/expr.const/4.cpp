// [expr.const] Example 4 (paragraph 9), docs/cxx-spec/expr/expr.const.md
// needs: P5
// status: annotated

struct A {
  constexpr A(int i) : val(i) { }
  constexpr operator int() const { return val; }
  constexpr operator long() const { return 42; }
private:
  int val;
};
constexpr A a = alignof(int);
alignas(a) int n;               // error: ambiguous conversion // semantic
struct B { int n : a; };        // error: ambiguous conversion // semantic
