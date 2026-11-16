// [namespace.udir] Example 2 (paragraph 4), docs/cxx-spec/dcl/namespace.udir.md
// needs: P5
// status: annotated

namespace M {
  int i;
}

namespace N {
  int i;
  using namespace M;
}

void f() {
  using namespace N;
  i = 7;            // error: both M::i and N::i are visible // semantic
}
