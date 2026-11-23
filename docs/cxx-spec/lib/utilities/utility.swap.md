# 20.2.2 `swap` [utility.swap]

[Contents](../../README.md) · up: [20.2 Utility components \[utility\]](utility.md) · prev: [20.2.1 Header `<utility>` synopsis \[utility.syn\]](utility.syn.md) · next: [20.2.3 `exchange` \[utility.exchange\]](utility.exchange.md)

```cpp
template<class T>
  constexpr void swap(T& a, T& b) noexcept(see below);
```

**1** *Constraints:* `is_move_constructible_v<T>` is `true` and `is_move_assignable_v<T>` is `true`.

**2** *Preconditions:* Type `T` meets the *Cpp17MoveConstructible* ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17MoveAssignable* ([Table 30](../library/utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.

**3** *Effects:* Exchanges values stored in two locations.

**4** *Remarks:* This function is a designated customization point ([[namespace.std]](../library/namespace.std.md)). The expression inside `noexcept` is equivalent to:

```cpp
is_nothrow_move_constructible_v<T> && is_nothrow_move_assignable_v<T>
```

```cpp
template<class T, size_t N>
  constexpr void swap(T (&a)[N], T (&b)[N]) noexcept(is_nothrow_swappable_v<T>);
```

**5** *Constraints:* `is_swappable_v<T>` is `true`.

**6** *Preconditions:* `a[i]` is swappable with ([[swappable.requirements]](../library/swappable.requirements.md)) `b[i]` for all `i` in the range `[0, N)`.

**7** *Effects:* As if by `swap_ranges(a, a + N, b)`.
