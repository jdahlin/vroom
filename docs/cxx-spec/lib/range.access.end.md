# 24.3.2 `ranges::end` [range.access.end]

[Contents](../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.1 `ranges::begin` \[range.access.begin\]](range.access.begin.md) · next: [24.3.3 `ranges::cbegin` \[range.access.cbegin\]](range.access.cbegin.md)

**1** The name `ranges::end` denotes a customization point object ([[customization.point.object]](customization.point.object.md)).

**2** Given a subexpression `E` with type `T`, let `t` be an lvalue that denotes the reified object for `E`. Then:

- If `E` is an rvalue and `enable_borrowed_range<remove_cv_t<T>>` is `false`, `ranges::end(E)` is ill-formed.

- Otherwise, if `T` is an array type ([[basic.compound]](../basic.compound.md)) and `remove_all_extents_t<T>` is an incomplete type, `ranges::end(E)` is ill-formed with no diagnostic required.

- Otherwise, if `T` is an array of unknown bound, `ranges::end(E)` is ill-formed.

- Otherwise, if `T` is an array, `ranges::end(E)` is expression-equivalent to `t + extent_v<T>`.

- Otherwise, if `decay-copy(t.end())` is a valid expression whose type models `sentinel_for<iterator_t<T>>` then `ranges::end(E)` is expression-equivalent to `decay-copy(t.end())`.

- Otherwise, if `T` is a class or enumeration type and `decay-copy(end(t))` is a valid expression whose type models `sentinel_for<iterator_t<T>>` with overload resolution performed in a context in which unqualified lookup for `end` finds only the declarations

  ```cpp
  void end(auto&) = delete;
  void end(const auto&) = delete;
  ```

  then `ranges::end(E)` is expression-equivalent to `decay-copy(end(t))` with overload resolution performed in the above context.

- Otherwise, `ranges::end(E)` is ill-formed.

**3** [*Note 1*: Diagnosable ill-formed cases above result in substitution failure when `ranges::end(E)` appears in the immediate context of a template instantiation. — *end note*]

**4** [*Note 2*: Whenever `ranges::end(E)` is a valid expression, the types `S` and `I` of `ranges::end(E)` and `ranges::begin(E)` model `sentinel_for<S, I>`. — *end note*]
