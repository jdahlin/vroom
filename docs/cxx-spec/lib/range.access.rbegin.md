# 24.3.5 `ranges::rbegin` [range.access.rbegin]

[Contents](../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.4 `ranges::cend` \[range.access.cend\]](range.access.cend.md) · next: [24.3.6 `ranges::rend` \[range.access.rend\]](range.access.rend.md)

**1** The name `ranges::rbegin` denotes a customization point object ([[customization.point.object]](customization.point.object.md)).

**2** Given a subexpression `E` with type `T`, let `t` be an lvalue that denotes the reified object for `E`. Then:

- If `E` is an rvalue and `enable_borrowed_range<remove_cv_t<T>>` is `false`, `ranges::rbegin(E)` is ill-formed.

- Otherwise, if `T` is an array type ([[basic.compound]](../basic.compound.md)) and `remove_all_extents_t<T>` is an incomplete type, `ranges::rbegin(E)` is ill-formed with no diagnostic required.

- Otherwise, if `decay-copy(t.rbegin())` is a valid expression whose type models `input_or_output_iterator`, `ranges::rbegin(E)` is expression-equivalent to `decay-copy(t.rbegin())`.

- Otherwise, if `T` is a class or enumeration type and `decay-copy(rbegin(t))` is a valid expression whose type models `input_or_output_iterator` with overload resolution performed in a context in which unqualified lookup for `rbegin` finds only the declarations

  ```cpp
  void rbegin(auto&) = delete;
  void rbegin(const auto&) = delete;
  ```

  then `ranges::rbegin(E)` is expression-equivalent to `decay-copy(rbegin(t))` with overload resolution performed in the above context.

- Otherwise, if both `ranges::begin(t)` and `ranges::end(t)` are valid expressions of the same type which models `bidirectional_iterator` ([[iterator.concept.bidir]](iterator.concept.bidir.md)), `ranges::rbegin(E)` is expression-equivalent to `make_reverse_iterator(ranges::end(t))`.

- Otherwise, `ranges::rbegin(E)` is ill-formed.

**3** [*Note 1*: Diagnosable ill-formed cases above result in substitution failure when `ranges::rbegin(E)` appears in the immediate context of a template instantiation. — *end note*]

**4** [*Note 2*: Whenever `ranges::rbegin(E)` is a valid expression, its type models `input_or_output_iterator`. — *end note*]
