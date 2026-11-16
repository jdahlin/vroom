// [dcl.fct.def.delete] Example 1 (paragraph 3), docs/cxx-spec/dcl/dcl.fct.def.delete.md
// needs: P7
// status: well-formed (as far as the example says)

struct onlydouble {
  onlydouble() = delete;                // OK, but redundant
  template<class T>
    onlydouble(T) = delete;
  onlydouble(double);
};
