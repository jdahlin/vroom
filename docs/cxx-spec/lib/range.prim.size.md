# 24.3.9 `ranges::size` [range.prim.size]

[Contents](../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.8 `ranges::crend` \[range.access.crend\]](range.access.crend.md) · next: [24.3.10 `ranges::ssize` \[range.prim.ssize\]](range.prim.ssize.md)

**1** The name `ranges::size` denotes a customization point object ([[customization.point.object]](customization.point.object.md)).

**2** Given a subexpression `E` with type `T`, let `t` be an lvalue that denotes the reified object for `E`. Then:

- If `T` is an array of unknown bound ([[dcl.array]](../dcl.array.md)), `ranges::size(E)` is ill-formed.

- Otherwise, if `T` is an array type, `ranges::size(E)` is expression-equivalent to `decay-copy(extent_v<T>)`.

- Otherwise, if `disable_sized_range<remove_cv_t<T>>` ([[range.sized]](range.sized.md)) is `false` and `decay-copy(t.size())` is a valid expression of integer-like type ([[iterator.concept.winc]](iterator.concept.winc.md)), `ranges::size(E)` is expression-equivalent to `decay-copy(t.size())`.

- Otherwise, if `T` is a class or enumeration type, `disable_sized_range<remove_cv_t<T>>` is `false` and `decay-copy(size(t))` is a valid expression of integer-like type with overload resolution performed in a context in which unqualified lookup for `size` finds only the declarations

  ```cpp
  void size(auto&) = delete;
  void size(const auto&) = delete;
  ```

  then `ranges::size(E)` is expression-equivalent to `decay-copy(size(t))` with overload resolution performed in the above context.

- Otherwise, if `to-unsigned-like(ranges::end(t) - ranges::begin(t))` ([[ranges.syn]](ranges.syn.md)) is a valid expression and the types `I` and `S` of `ranges::begin(t)` and `ranges::end(t)` (respectively) model both `sized_sentinel_for<S, I>` ([[iterator.concept.sizedsentinel]](iterator.concept.sizedsentinel.md)) and `forward_iterator<I>`, then `ranges::size(E)` is expression-equivalent to `to-unsigned-like(ranges::end(t) - ranges::begin(t))`.

- Otherwise, `ranges::size(E)` is ill-formed.

**3** [*Note 1*: Diagnosable ill-formed cases above result in substitution failure when `ranges::size(E)` appears in the immediate context of a template instantiation. — *end note*]

**4** [*Note 2*: Whenever `ranges::size(E)` is a valid expression, its type is integer-like. — *end note*]
