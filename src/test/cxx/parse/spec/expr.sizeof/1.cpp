// [expr.sizeof] Example 1 (paragraph 4), docs/cxx-spec/expr/expr.sizeof.md
// needs: P7
// status: well-formed (as far as the example says)

template<class... Types>
struct count {
  static const std::size_t value = sizeof...(Types);
};
