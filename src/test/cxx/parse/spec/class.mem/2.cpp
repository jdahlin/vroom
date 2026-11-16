// [class.mem] Example 2 (paragraph 9), docs/cxx-spec/class/class.mem.md
// needs: P5
// status: annotated

int a;
const int b = 0;
struct S {
  int x1 : 8 = 42;              // OK, "= 42" is brace-or-equal-initializer
  int x2 : 8 { 42 };            // OK, "{ 42 }" is brace-or-equal-initializer
  int y1 : true ? 8 : a = 42;   // OK, brace-or-equal-initializer is absent
  int y2 : true ? 8 : b = 42;   // error: cannot assign to const int // semantic
  int y3 : (true ? 8 : b) = 42; // OK, "= 42" is brace-or-equal-initializer
  int z : 1 || new int { 0 };   // OK, brace-or-equal-initializer is absent
};
