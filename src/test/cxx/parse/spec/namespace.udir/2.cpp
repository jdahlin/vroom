// [namespace.udir] Example 2 (paragraph 4), docs/cxx-spec/dcl/namespace.udir.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

namespace M {
  int i;
}

namespace N {
  int i;
  using namespace M;
}

void f() {
  using namespace N;
  i = 7;            // error: both M::i and N::i are visible
}
