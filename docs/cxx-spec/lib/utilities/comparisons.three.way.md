# 20.14.7.7 Class `compare_three_way` [comparisons.three.way]

[Contents](../../README.md) · up: [20.14.7 Comparisons \[comparisons\]](comparisons.md) · prev: [20.14.7.6 Class template `less_equal` \[comparisons.less.equal\]](comparisons.less.equal.md) · next: [20.14.8 Concept-constrained comparisons \[range.cmp\]](range.cmp.md)

**1** In this subclause, `BUILTIN-PTR-THREE-WAY(T, U)` for types `T` and `U` is a boolean constant expression. `BUILTIN-PTR-THREE-WAY(T, U)` is `true` if and only if `<=>` in the expression

```cpp
declval<T>() <=> declval<U>()
```

resolves to a built-in operator comparing pointers.

```cpp
struct compare_three_way {
  template<class T, class U>
    requires three_way_comparable_with<T, U> || BUILTIN-PTR-THREE-WAY(T, U)
  constexpr auto operator()(T&& t, U&& u) const;

  using is_transparent = unspecified;
};
```

```cpp
template<class T, class U>
  requires three_way_comparable_with<T, U> || BUILTIN-PTR-THREE-WAY(T, U)
constexpr auto operator()(T&& t, U&& u) const;
```

**2** *Preconditions:* If the expression `std::forward<T>(t) <=> std::forward<U>(u)` results in a call to a built-in operator `<=>` comparing pointers of type `P`, the conversion sequences from both `T` and `U` to `P` are equality-preserving ([[concepts.equality]](../concepts/concepts.equality.md)).

**3** *Effects:*

- If the expression `std::forward<T>(t) <=> std::forward<U>(u)` results in a call to a built-in operator `<=>` comparing pointers of type `P`, returns `strong_ordering::less` if (the converted value of) `t` precedes `u` in the implementation-defined strict total order over pointers ([[defns.order.ptr]](../library/definitions.md#defns.order.ptr)), `strong_ordering::greater` if `u` precedes `t`, and otherwise `strong_ordering::equal`.
- Otherwise, equivalent to: `return std::forward<T>(t) <=> std::forward<U>(u);`
