# 17.11.5 Result of three-way comparison [cmp.result]

[Contents](../../README.md) · up: [17.11 Comparisons \[cmp\]](cmp.md) · prev: [17.11.4 Concept `three_way_comparable` \[cmp.concept\]](cmp.concept.md) · next: [17.11.6 Comparison algorithms \[cmp.alg\]](cmp.alg.md)

**1** The behavior of a program that adds specializations for the `compare_three_way_result` template defined in this subclause is undefined.

**2** For the `compare_three_way_result` type trait applied to the types `T` and `U`, let `t` and `u` denote lvalues of types `const remove_reference_t<T>` and `const remove_reference_t<U>`, respectively. If the expression `t <=> u` is well-formed when treated as an unevaluated operand ([[expr.context]](../../expr/expr.context.md)), the member *typedef-name* `type` denotes the type `decltype(t <=> u)`. Otherwise, there is no member `type`.
