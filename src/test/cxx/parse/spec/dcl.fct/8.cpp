// [dcl.fct] Example 8 (paragraph 19), docs/cxx-spec/dcl/dcl.fct.md
// needs: P7
// status: well-formed (as far as the example says)

template<typename> concept C = /* ... */;

template <typename T, C U>
  void g(T x, U y, C auto z);
