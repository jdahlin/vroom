// [expr.new] Example 1 (paragraph 2), docs/cxx-spec/expr/expr.new.md
// needs: P7
// status: well-formed (as far as the example says)

new auto(1);                    // allocated type is int
auto x = new auto('a');         // allocated type is char, x is of type char*

template<class T> struct A { A(T, T); };
auto y = new A{1, 2};           // allocated type is A<int>
