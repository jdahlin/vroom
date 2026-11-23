// [class.temporary] Example 2 (paragraph 6), docs/cxx-spec/basic/class.temporary.md
// needs: P7
// status: well-formed (as far as the example says)

template<typename T> using id = T;

int i = 1;
int&& a = id<int[3]>{1, 2, 3}[i];           // temporary array has same lifetime as a
const int& b = static_cast<const int&>(0);  // temporary int has same lifetime as b
int&& c = cond ? id<int[3]>{1, 2, 3}[i] : static_cast<int&&>(0);
                                            // exactly one of the two temporaries is lifetime-extended
