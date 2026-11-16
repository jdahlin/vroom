// [dcl.enum] Example 4 (paragraph 11), docs/cxx-spec/dcl/dcl.enum.md
// needs: P4
// status: annotated

enum direction { left='l', right='r' };

void g()  {
  direction d;                  // OK
  d = left;                     // OK
  d = direction::right;         // OK
}

enum class altitude { high='h', low='l' };

void h()  {
  altitude a;                   // OK
  a = high;                     // error: high not in scope // semantic
  a = altitude::low;            // OK
}
