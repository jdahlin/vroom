# 24.3.1 `ranges::begin` [range.access.begin]

[Contents](../../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3 Range access \[range.access\]](range.access.md) · next: [24.3.2 `ranges::end` \[range.access.end\]](range.access.end.md)

**1** The name `ranges::begin` denotes a customization point object ([[customization.point.object]](../library/customization.point.object.md)).

**2** Given a subexpression `E` with type `T`, let `t` be an lvalue that denotes the reified object for `E`. Then:

- If `E` is an rvalue and `enable_borrowed_range<remove_cv_t<T>>` is `false`, `ranges::begin(E)` is ill-formed.

- Otherwise, if `T` is an array type ([[basic.compound]](../../basic/basic.compound.md)) and `remove_all_extents_t<T>` is an incomplete type, `ranges::begin(E)` is ill-formed with no diagnostic required.

- Otherwise, if `T` is an array type, `ranges::begin(E)` is expression-equivalent to `t + 0`.

- Otherwise, if `decay-copy(t.begin())` is a valid expression whose type models `input_or_output_iterator`, `ranges::begin(E)` is expression-equivalent to `decay-copy(t.begin())`.

- Otherwise, if `T` is a class or enumeration type and `decay-copy(begin(t))` is a valid expression whose type models `input_or_output_iterator` with overload resolution performed in a context in which unqualified lookup for `begin` finds only the declarations

  ```cpp
  void begin(auto&) = delete;
  void begin(const auto&) = delete;
  ```

  then `ranges::begin(E)` is expression-equivalent to `decay-copy(begin(t))` with overload resolution performed in the above context.

- Otherwise, `ranges::begin(E)` is ill-formed.

**3** [*Note 1*: Diagnosable ill-formed cases above result in substitution failure when `ranges::begin(E)` appears in the immediate context of a template instantiation. — *end note*]

**4** [*Note 2*: Whenever `ranges::begin(E)` is a valid expression, its type models `input_or_output_iterator`. — *end note*]
