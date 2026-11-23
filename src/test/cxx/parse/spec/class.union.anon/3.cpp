// [class.union.anon] Example 3 (paragraph 4), docs/cxx-spec/class/class.union.anon.md
// needs: P5
// status: annotated

union U {
  int x = 0;
  union {
    int k;
  };
  union {
    int z;
    int y = 1;      // error: initialization for second variant member of U // semantic
  };
};
