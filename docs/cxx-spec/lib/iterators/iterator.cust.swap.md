# 23.3.3.2 `ranges::iter_swap` [iterator.cust.swap]

[Contents](../../README.md) · up: [23.3.3 Customization points \[iterator.cust\]](iterator.cust.md) · prev: [23.3.3.1 `ranges::iter_move` \[iterator.cust.move\]](iterator.cust.move.md) · next: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md)

**1** The name `ranges::iter_swap` denotes a customization point object ([[customization.point.object]](../library/customization.point.object.md)) that exchanges the values ([[concept.swappable]](../concepts/concept.swappable.md)) denoted by its arguments.

**2** Let `iter-exchange-move` be the exposition-only function:

```cpp
template<class X, class Y>
  constexpr iter_value_t<X> iter-exchange-move(X&& x, Y&& y)
    noexcept(noexcept(iter_value_t<X>(iter_move(x))) &&
             noexcept(*x = iter_move(y)));
```

**3** *Effects:* Equivalent to:

```cpp
iter_value_t<X> old_value(iter_move(x));
*x = iter_move(y);
return old_value;
```

**4** The expression `ranges::iter_swap(E1, E2)` for subexpressions `E1` and `E2` is expression-equivalent to:

- `(void)iter_swap(E1, E2)`, if either `E1` or `E2` has class or enumeration type and `iter_swap(E1, E2)` is a well-formed expression with overload resolution performed in a context that includes the declaration

  ```cpp
  template<class I1, class I2>
    void iter_swap(I1, I2) = delete;
  ```

  and does not include a declaration of `ranges::iter_swap`. If the function selected by overload resolution does not exchange the values denoted by `E1` and `E2`, the program is ill-formed, no diagnostic required.

- Otherwise, if the types of `E1` and `E2` each model `indirectly_readable`, and if the reference types of `E1` and `E2` model `swappable_with` ([[concept.swappable]](../concepts/concept.swappable.md)), then `ranges::swap(*E1, *E2)`.

- Otherwise, if the types `T1` and `T2` of `E1` and `E2` model `indirectly_movable_storable<T1, T2>` and `indirectly_movable_storable<T2, T1>`, then `(void)(*E1 = iter-exchange-move(E2, E1))`, except that `E1` is evaluated only once.

- Otherwise, `ranges::iter_swap(E1, E2)` is ill-formed. [*Note 1*: This case can result in substitution failure when `ranges::iter_swap(E1, E2)` appears in the immediate context of a template instantiation. — *end note*]
