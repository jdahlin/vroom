// [dcl.fct] Example 7, part 3 (paragraph 18), docs/cxx-spec/dcl/dcl.fct.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

template<> void g1<int>(const int*, const double&); // OK, specialization of g1<int, const double>
