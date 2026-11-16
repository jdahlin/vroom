// [class.conv] Example 2 (paragraph 5), docs/cxx-spec/class/class.conv.md
// needs: P5
// status: annotated

struct X {
  operator int();
};

struct Y : X {
    operator char();
};

void f(Y& a) {
  if (a) {          // error: ambiguous between X::operator int() and Y::operator char() // semantic
  }
}
