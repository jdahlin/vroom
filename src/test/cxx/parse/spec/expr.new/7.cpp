// [expr.new] Example 7 (paragraph 27), docs/cxx-spec/expr/expr.new.md
// needs: P5
// status: annotated

struct S {
  // Placement allocation function:
  static void* operator new(std::size_t, std::size_t);

  // Usual (non-placement) deallocation function:
  static void operator delete(void*, std::size_t);
};

S* p = new (0) S;   // error: non-placement deallocation function matches // semantic
                    // placement allocation function
