// [class.union] Example 3 (paragraph 7), docs/cxx-spec/class/class.union.md
// needs: P5
// status: well-formed (as far as the example says)

u.m.~M();
new (&u.n) N;
