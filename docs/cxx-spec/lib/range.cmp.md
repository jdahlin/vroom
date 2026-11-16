# 20.14.8 Concept-constrained comparisons [range.cmp]

[Contents](../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.7.7 Class `compare_three_way` \[comparisons.three.way\]](comparisons.three.way.md) · next: [20.14.9 Logical operations \[logical.operations\]](logical.operations.md)

**1** In this subclause, `BUILTIN-PTR-CMP(T, op, U)` for types `T` and `U` and where *op* is an equality ([[expr.eq]](../expr.eq.md)) or relational operator ([[expr.rel]](../expr.rel.md)) is a boolean constant expression. `BUILTIN-PTR-CMP(T, op, U)` is `true` if and only if *op* in the expression `declval<T>() op declval<U>()` resolves to a built-in operator comparing pointers.

```cpp
struct ranges::equal_to {
  template<class T, class U>
    requires equality_comparable_with<T, U> || BUILTIN-PTR-CMP(T, ==, U)
  constexpr bool operator()(T&& t, U&& u) const;

  using is_transparent = unspecified;
};
```

**2** *Preconditions:* If the expression `std::forward<T>(t) == std::forward<U>(u)` results in a call to a built-in operator `==` comparing pointers of type `P`, the conversion sequences from both `T` and `U` to `P` are equality-preserving ([[concepts.equality]](concepts.equality.md)).

**3** *Effects:*

- If the expression `std::forward<T>(t) == std::forward<U>(u)` results in a call to a built-in operator `==` comparing pointers: returns `false` if either (the converted value of) `t` precedes `u` or `u` precedes `t` in the implementation-defined strict total order over pointers ([[defns.order.ptr]](definitions.md#defns.order.ptr)) and otherwise `true`.
- Otherwise, equivalent to: `return std::forward<T>(t) == std::forward<U>(u);`

```cpp
struct ranges::not_equal_to {
  template<class T, class U>
    requires equality_comparable_with<T, U> || BUILTIN-PTR-CMP(T, ==, U)
  constexpr bool operator()(T&& t, U&& u) const;

  using is_transparent = unspecified;
};
```

**4** `operator()` has effects equivalent to:

```cpp
return !ranges::equal_to{}(std::forward<T>(t), std::forward<U>(u));
```

```cpp
struct ranges::greater {
  template<class T, class U>
    requires totally_ordered_with<T, U> || BUILTIN-PTR-CMP(U, <, T)
  constexpr bool operator()(T&& t, U&& u) const;

  using is_transparent = unspecified;
};
```

**5** `operator()` has effects equivalent to:

```cpp
return ranges::less{}(std::forward<U>(u), std::forward<T>(t));
```

```cpp
struct ranges::less {
  template<class T, class U>
    requires totally_ordered_with<T, U> || BUILTIN-PTR-CMP(T, <, U)
  constexpr bool operator()(T&& t, U&& u) const;

  using is_transparent = unspecified;
};
```

**6** *Preconditions:* If the expression `std::forward<T>(t) < std::forward<U>(u)` results in a call to a built-in operator `<` comparing pointers of type `P`, the conversion sequences from both `T` and `U` to `P` are equality-preserving ([[concepts.equality]](concepts.equality.md)). For any expressions `ET` and `EU` such that `decltype((ET))` is `T` and `decltype((EU))` is `U`, exactly one of `ranges::less{}(ET, EU)`, `ranges::less{}(EU, ET)`, or `ranges::equal_to{}(ET, EU)` is `true`.

**7** *Effects:*

- If the expression `std::forward<T>(t) < std::forward<U>(u)` results in a call to a built-in operator `<` comparing pointers: returns `true` if (the converted value of) `t` precedes `u` in the implementation-defined strict total order over pointers ([[defns.order.ptr]](definitions.md#defns.order.ptr)) and otherwise `false`.
- Otherwise, equivalent to: `return std::forward<T>(t) < std::forward<U>(u);`

```cpp
struct ranges::greater_equal {
  template<class T, class U>
    requires totally_ordered_with<T, U> || BUILTIN-PTR-CMP(T, <, U)
  constexpr bool operator()(T&& t, U&& u) const;

  using is_transparent = unspecified;
};
```

**8** `operator()` has effects equivalent to:

```cpp
return !ranges::less{}(std::forward<T>(t), std::forward<U>(u));
```

```cpp
struct ranges::less_equal {
  template<class T, class U>
    requires totally_ordered_with<T, U> || BUILTIN-PTR-CMP(U, <, T)
  constexpr bool operator()(T&& t, U&& u) const;

  using is_transparent = unspecified;
};
```

**9** `operator()` has effects equivalent to:

```cpp
return !ranges::less{}(std::forward<U>(u), std::forward<T>(t));
```
