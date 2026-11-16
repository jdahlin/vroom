// [class.copy.elision] Example 3 (paragraph 5), docs/cxx-spec/class/class.copy.elision.md
// needs: P7
// status: annotated

template<class T> void g(const T&);

template<class T> void f() {
  T x;
  try {
    T y;
    try { g(x); }
    catch (...) {
      if (/*...*/)
        throw x;        // does not move
      throw y;          // moves
    }
    g(y);
  } catch(...) {
    g(x);
    g(y);               // error: y is not in scope // semantic
  }
}
