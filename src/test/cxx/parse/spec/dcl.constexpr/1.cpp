// [dcl.constexpr] Example 1 (paragraph 1), docs/cxx-spec/dcl/dcl.constexpr.md
// needs: P5
// status: annotated

constexpr void square(int &x);  // OK: declaration
constexpr int bufsz = 1024;     // OK: definition
constexpr struct pixel {        // error: pixel is a type // semantic
  int x;
  int y;
  constexpr pixel(int);         // OK: declaration
};
constexpr pixel::pixel(int a)
  : x(a), y(x)                  // OK: definition
  { square(x); }
constexpr pixel small(2);       // error: square not defined, so small(2) // semantic
                                // not constant ([expr.const]) so constexpr not satisfied

constexpr void square(int &x) { // OK: definition
  x *= x;
}
constexpr pixel large(4);       // OK: square defined
int next(constexpr int x) {     // error: not for parameters // semantic
     return x + 1;
}
extern constexpr int memsz;     // error: not a definition // semantic
