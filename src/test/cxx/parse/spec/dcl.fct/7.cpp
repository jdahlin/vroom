// [dcl.fct] Example 7 (paragraph 18), docs/cxx-spec/dcl/dcl.fct.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

template<typename T>     concept C1 = /* ... */;
template<typename T>     concept C2 = /* ... */;
template<typename... Ts> concept C3 = /* ... */;

void g1(const C1 auto*, C2 auto&);
void g2(C1 auto&...);
void g3(C3 auto...);
void g4(C3 auto);
