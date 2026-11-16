# 20.4.2 Class template `pair` [pairs.pair]

[Contents](../../README.md) · up: [20.4 Pairs \[pairs\]](pairs.md) · prev: [20.4.1 In general \[pairs.general\]](pairs.general.md) · next: [20.4.3 Specialized algorithms \[pairs.spec\]](pairs.spec.md)

```cpp
namespace std {
  template<class T1, class T2>
  struct pair {
    using first_type  = T1;
    using second_type = T2;

    T1 first;
    T2 second;

    pair(const pair&) = default;
    pair(pair&&) = default;
    constexpr explicit(see below) pair();
    constexpr explicit(see below) pair(const T1& x, const T2& y);
    template<class U1, class U2>
      constexpr explicit(see below) pair(U1&& x, U2&& y);
    template<class U1, class U2>
      constexpr explicit(see below) pair(const pair<U1, U2>& p);
    template<class U1, class U2>
      constexpr explicit(see below) pair(pair<U1, U2>&& p);
    template<class... Args1, class... Args2>
      constexpr pair(piecewise_construct_t,
                     tuple<Args1...> first_args, tuple<Args2...> second_args);

    constexpr pair& operator=(const pair& p);
    template<class U1, class U2>
      constexpr pair& operator=(const pair<U1, U2>& p);
    constexpr pair& operator=(pair&& p) noexcept(see below);
    template<class U1, class U2>
      constexpr pair& operator=(pair<U1, U2>&& p);

    constexpr void swap(pair& p) noexcept(see below);
  };

  template<class T1, class T2>
    pair(T1, T2) -> pair<T1, T2>;
}
```

**1** Constructors and member functions of `pair` do not throw exceptions unless one of the element-wise operations specified to be called for that operation throws an exception.

**2** The defaulted move and copy constructor, respectively, of `pair` is a constexpr function if and only if all required element-wise initializations for move and copy, respectively, would satisfy the requirements for a constexpr function.

**3** If `(is_trivially_destructible_v<T1> && is_trivially_destructible_v<T2>)` is `true`, then the destructor of `pair` is trivial.

**4** `pair<T, U>` is a structural type ([[temp.param]](../../temp/temp.param.md)) if `T` and `U` are both structural types. Two values `p1` and `p2` of type `pair<T, U>` are template-argument-equivalent ([[temp.type]](../../temp/temp.type.md)) if and only if `p1.first` and `p2.first` are template-argument-equivalent and `p1.second` and `p2.second` are template-argument-equivalent.

```cpp
constexpr explicit(see below) pair();
```

**5** *Constraints:*

- `is_default_constructible_v<first_type>` is `true` and
- `is_default_constructible_v<second_type>` is `true`.

**6** *Effects:* Value-initializes `first` and `second`.

**7** *Remarks:* The expression inside `explicit` evaluates to `true` if and only if either `first_type` or `second_type` is not implicitly default-constructible. [*Note 1*: This behavior can be implemented with a trait that checks whether a `const first_type&` or a `const second_type&` can be initialized with `{}`. — *end note*]

```cpp
constexpr explicit(see below) pair(const T1& x, const T2& y);
```

**8** *Constraints:*

- `is_copy_constructible_v<first_type>` is `true` and
- `is_copy_constructible_v<second_type>` is `true`.

**9** *Effects:* Initializes `first` with `x` and `second` with `y`.

**10** *Remarks:* The expression inside `explicit` is equivalent to:

```cpp
!is_convertible_v<const first_type&, first_type> ||
  !is_convertible_v<const second_type&, second_type>
```

```cpp
template<class U1, class U2> constexpr explicit(see below) pair(U1&& x, U2&& y);
```

**11** *Constraints:*

- `is_constructible_v<first_type, U1>` is `true` and
- `is_constructible_v<second_type, U2>` is `true`.

**12** *Effects:* Initializes `first` with `std::forward<U1>(x)` and `second` with `std::forward<U2>(y)`.

**13** *Remarks:* The expression inside `explicit` is equivalent to:

```cpp
!is_convertible_v<U1, first_type> || !is_convertible_v<U2, second_type>
```

```cpp
template<class U1, class U2> constexpr explicit(see below) pair(const pair<U1, U2>& p);
```

**14** *Constraints:*

- `is_constructible_v<first_type, const U1&>` is `true` and
- `is_constructible_v<second_type, const U2&>` is `true`.

**15** *Effects:* Initializes members from the corresponding members of the argument.

**16** *Remarks:* The expression inside explicit is equivalent to:

```cpp
!is_convertible_v<const U1&, first_type> || !is_convertible_v<const U2&, second_type>
```

```cpp
template<class U1, class U2> constexpr explicit(see below) pair(pair<U1, U2>&& p);
```

**17** *Constraints:*

- `is_constructible_v<first_type, U1>` is `true` and
- `is_constructible_v<second_type, U2>` is `true`.

**18** *Effects:* Initializes `first` with `std::forward<U1>(p.first)` and `second` with `std::forward<U2>(p.second)`.

**19** *Remarks:* The expression inside explicit is equivalent to:

```cpp
!is_convertible_v<U1, first_type> || !is_convertible_v<U2, second_type>
```

```cpp
template<class... Args1, class... Args2>
  constexpr pair(piecewise_construct_t,
                 tuple<Args1...> first_args, tuple<Args2...> second_args);
```

**20** *Mandates:*

- `is_constructible_v<first_type, Args1...>` is `true` and
- `is_constructible_v<second_type, Args2...>` is `true`.

**21** *Effects:* Initializes `first` with arguments of types `Args1...` obtained by forwarding the elements of `first_args` and initializes `second` with arguments of types `Args2...` obtained by forwarding the elements of `second_args`. (Here, forwarding an element `x` of type `U` within a `tuple` object means calling `std::forward<U>(x)`.) This form of construction, whereby constructor arguments for `first` and `second` are each provided in a separate `tuple` object, is called *piecewise construction*.

```cpp
constexpr pair& operator=(const pair& p);
```

**22** *Effects:* Assigns `p.first` to `first` and `p.second` to `second`.

**23** *Remarks:* This operator is defined as deleted unless `is_copy_assignable_v<first_type>` is `true` and `is_copy_assignable_v<second_type>` is `true`.

**24** *Returns:* `*this`.

```cpp
template<class U1, class U2> constexpr pair& operator=(const pair<U1, U2>& p);
```

**25** *Constraints:*

- `is_assignable_v<first_type&, const U1&>` is `true` and
- `is_assignable_v<second_type&, const U2&>` is `true`.

**26** *Effects:* Assigns `p.first` to `first` and `p.second` to `second`.

**27** *Returns:* `*this`.

```cpp
constexpr pair& operator=(pair&& p) noexcept(see below);
```

**28** *Constraints:*

- `is_move_assignable_v<first_type>` is `true` and
- `is_move_assignable_v<second_type>` is `true`.

**29** *Effects:* Assigns to `first` with `std::forward<first_type>(p.first)` and to `second` with<br> `std::forward<second_type>(p.second)`.

**30** *Returns:* `*this`.

**31** *Remarks:* The expression inside `noexcept` is equivalent to:

```cpp
is_nothrow_move_assignable_v<T1> && is_nothrow_move_assignable_v<T2>
```

```cpp
template<class U1, class U2> constexpr pair& operator=(pair<U1, U2>&& p);
```

**32** *Constraints:*

- `is_assignable_v<first_type&, U1>` is `true` and
- `is_assignable_v<second_type&, U2>` is `true`.

**33** *Effects:* Assigns to `first` with `std::forward<U1>(p.first)` and to `second` with<br> `std::forward<U2>(p.second)`.

**34** *Returns:* `*this`.

```cpp
constexpr void swap(pair& p) noexcept(see below);
```

**35** *Preconditions:* `first` is swappable with ([[swappable.requirements]](../library/swappable.requirements.md)) `p.first` and `second` is swappable with `p.second`.

**36** *Effects:* Swaps `first` with `p.first` and `second` with `p.second`.

**37** *Remarks:* The expression inside `noexcept` is equivalent to:

```cpp
is_nothrow_swappable_v<first_type> && is_nothrow_swappable_v<second_type>
```
