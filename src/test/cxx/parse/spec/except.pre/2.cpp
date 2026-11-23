// [except.pre] Example 2 (paragraph 3), docs/cxx-spec/except/except.pre.md
// needs: P6
// status: well-formed (as far as the example says)

lab:  try {
  T1 t1;
  try {
    T2 t2;
    if (condition)
      goto lab;
    } catch(...) { /* handler 2 */ }
  } catch(...) { /* handler 1 */ }
