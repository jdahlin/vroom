// [over.oper] Example 1 (paragraph 4), docs/cxx-spec/over/over.oper.md
// needs: P5 (names declared in another part or outside the example stay unparsed)
// status: well-formed (as far as the example says)

complex z = a.operator+(b);     // complex z = a+b;
void* p = operator new(sizeof(int)*n);
