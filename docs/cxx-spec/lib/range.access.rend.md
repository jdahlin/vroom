# 24.3.6 `ranges::rend` [range.access.rend]

[Contents](../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.5 `ranges::rbegin` \[range.access.rbegin\]](range.access.rbegin.md) · next: [24.3.7 `ranges::crbegin` \[range.access.crbegin\]](range.access.crbegin.md)

**1** The name `ranges::rend` denotes a customization point object ([[customization.point.object]](customization.point.object.md)).

**2** Given a subexpression `E` with type `T`, let `t` be an lvalue that denotes the reified object for `E`. Then:

- If `E` is an rvalue and `enable_borrowed_range<remove_cv_t<T>>` is `false`, `ranges::rend(E)` is ill-formed.

- Otherwise, if `T` is an array type ([[basic.compound]](../basic.compound.md)) and `remove_all_extents_t<T>` is an incomplete type, `ranges::rend(E)` is ill-formed with no diagnostic required.

- Otherwise, if `decay-copy(t.rend())` is a valid expression whose type models `sentinel_for<decltype(ranges::rbegin(E))>` then `ranges::rend(E)` is expression-equivalent to `decay-copy(t.rend())`.

- Otherwise, if `T` is a class or enumeration type and `decay-copy(rend(t))` is a valid expression whose type models `sentinel_for<decltype(ranges::rbegin(E))>` with overload resolution performed in a context in which unqualified lookup for `rend` finds only the declarations

  ```cpp
  void rend(auto&) = delete;
  void rend(const auto&) = delete;
  ```

  then `ranges::rend(E)` is expression-equivalent to `decay-copy(rend(t))` with overload resolution performed in the above context.

- Otherwise, if both `ranges::begin(t)` and `ranges::end(t)` are valid expressions of the same type which models `bidirectional_iterator` ([[iterator.concept.bidir]](iterator.concept.bidir.md)), then `ranges::rend(E)` is expression-equivalent to `make_reverse_iterator(ranges::begin(t))`.

- Otherwise, `ranges::rend(E)` is ill-formed.

**3** [*Note 1*: Diagnosable ill-formed cases above result in substitution failure when `ranges::rend(E)` appears in the immediate context of a template instantiation. — *end note*]

**4** [*Note 2*: Whenever `ranges::rend(E)` is a valid expression, the types `S` and `I` of `ranges::rend(E)` and `ranges::rbegin(E)` model `sentinel_for<S, I>`. — *end note*]
