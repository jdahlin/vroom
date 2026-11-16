// [class.nested.type] Example 1 (paragraph 1), docs/cxx-spec/class/class.nested.type.md
// needs: P5 (the names out of scope stay unparsed)
// status: annotated

struct X {
  typedef int I;
  class Y { /* ... */ };
  I a;
};

I b;                            // error // semantic
Y c;                            // error // semantic
X::Y d;                         // OK
X::I e;                         // OK
