# 24.4.3 Sized ranges [range.sized]

[Contents](../../README.md) · up: [24.4 Range requirements \[range.req\]](range.req.md) · prev: [24.4.2 Ranges \[range.range\]](range.range.md) · next: [24.4.4 Views \[range.view\]](range.view.md)

**1** The `sized_range` concept refines `range` with the requirement that the number of elements in the range can be determined in amortized constant time using `ranges::size`.

```cpp
template<class T>
  concept sized_range =
    range<T> &&
    requires(T& t) { ranges::size(t); };
```

**2** Given an lvalue `t` of type `remove_reference_t<T>`, `T` models `sized_range` only if

- `ranges::size(t)` is amortized 𝑂(1), does not modify `t`, and is equal to `ranges::distance(t)`, and
- if `iterator_t<T>` models `forward_iterator`, `ranges::size(t)` is well-defined regardless of the evaluation of `ranges::begin(t)`. [*Note 1*: `ranges::size(t)` is otherwise not required to be well-defined after evaluating `ranges::begin(t)`. For example, `ranges::size(t)` might be well-defined for a `sized_range` whose iterator type does not model `forward_iterator` only if evaluated before the first call to `ranges::begin(t)`. — *end note*]

```cpp
template<class>
  inline constexpr bool disable_sized_range = false;
```

**3** *Remarks:* Pursuant to [[namespace.std]](../library/namespace.std.md), users may specialize `disable_sized_range` for cv-unqualified program-defined types. Such specializations shall be usable in constant expressions ([[expr.const]](../../expr/expr.const.md)) and have type `const bool`.

**4** [*Note 2*: `disable_sized_range` allows use of range types with the library that satisfy but do not in fact model `sized_range`. — *end note*]
