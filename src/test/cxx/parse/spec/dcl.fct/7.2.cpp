// [dcl.fct] Example 7, part 2 (paragraph 18), docs/cxx-spec/dcl/dcl.fct.md
// needs: P7
// status: well-formed (as far as the example says)

template<C1 T, C2 U> void g1(const T*, U&);
template<C1... Ts>   void g2(Ts&...);
template<C3... Ts>   void g3(Ts...);
template<C3 T>       void g4(T);
