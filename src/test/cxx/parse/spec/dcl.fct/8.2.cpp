// [dcl.fct] Example 8, part 2 (paragraph 19), docs/cxx-spec/dcl/dcl.fct.md
// needs: P7
// status: well-formed (as far as the example says)

template<typename T, C U, C W>
  void g(T x, U y, W z);

template<typename T, typename U, typename W>
  requires C<U> && C<W>
  void g(T x, U y, W z);
