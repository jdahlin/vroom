// [class.conv] Example 1 (paragraph 4), docs/cxx-spec/class/class.conv.md
// needs: P5
// status: annotated

struct X {
  operator int();
};

struct Y {
  operator X();
};

Y a;
int b = a;          // error: no viable conversion (a.operator X().operator int() not considered) // semantic
int c = X(a);       // OK: a.operator X().operator int()
