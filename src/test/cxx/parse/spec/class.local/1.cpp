// [class.local] Example 1 (paragraph 1), docs/cxx-spec/class/class.local.md
// needs: P5 (the names out of scope stay unparsed)
// status: annotated

int x;
void f() {
  static int s;
  int x;
  const int N = 5;
  extern int q();
  int arr[2];
  auto [y, z] = arr;

  struct local {
    int g() { return x; }       // error: odr-use of non-odr-usable variable x // semantic
    int h() { return s; }       // OK
    int k() { return ::x; }     // OK
    int l() { return q(); }     // OK
    int m() { return N; }       // OK: not an odr-use
    int* n() { return &N; }     // error: odr-use of non-odr-usable variable N // semantic
    int p() { return y; }       // error: odr-use of non-odr-usable structured binding y // semantic
  };
}

local* p = 0;                   // error: local not in scope // semantic
