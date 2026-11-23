# 20.5.3.1 Construction [tuple.cnstr]

[Contents](../../README.md) · up: [20.5.3 Class template `tuple` \[tuple.tuple\]](tuple.tuple.md) · prev: [20.5.3 Class template `tuple` \[tuple.tuple\]](tuple.tuple.md) · next: [20.5.3.2 Assignment \[tuple.assign\]](tuple.assign.md)

**1** In the descriptions that follow, let *i* be in the range `[0, sizeof...(Types))` in order, `T`<sub>*i*</sub> be the *i*<sup></sup>*th* type in `Types`, and `U`<sub>*i*</sub> be the *i*<sup></sup>*th* type in a template parameter pack named `UTypes`, where indexing is zero-based.

**2** For each `tuple` constructor, an exception is thrown only if the construction of one of the types in `Types` throws an exception.

**3** The defaulted move and copy constructor, respectively, of `tuple` is a constexpr function if and only if all required element-wise initializations for move and copy, respectively, would satisfy the requirements for a constexpr function. The defaulted move and copy constructor of `tuple<>` are constexpr functions.

**4** If `is_trivially_destructible_v<T_i>` is `true` for all `T`<sub>*i*</sub>, then the destructor of `tuple` is trivial.

```cpp
constexpr explicit(see below) tuple();
```

**5** *Constraints:* `is_default_constructible_v<T_i>` is `true` for all *i*.

**6** *Effects:* Value-initializes each element.

**7** *Remarks:* The expression inside `explicit` evaluates to `true` if and only if `T`<sub>*i*</sub> is not copy-list-initializable from an empty list for at least one *i*. [*Note 1*: This behavior can be implemented with a trait that checks whether a `const T_i&` can be initialized with `{}`. — *end note*]

```cpp
constexpr explicit(see below) tuple(const Types&...);
```

**8** *Constraints:* `sizeof...(Types)` ≥ 1 and `is_copy_constructible_v<T_i>` is `true` for all *i*.

**9** *Effects:* Initializes each element with the value of the corresponding parameter.

**10** *Remarks:* The expression inside `explicit` is equivalent to:

```cpp
!conjunction_v<is_convertible<const Types&, Types>...>
```

```cpp
template<class... UTypes> constexpr explicit(see below) tuple(UTypes&&... u);
```

**11** *Constraints:* `sizeof...(Types)` equals `sizeof...(UTypes)` and `sizeof...(Types)` ≥ 1 and `is_constructible_v<T_i, U_i>` is `true` for all *i*.

**12** *Effects:* Initializes the elements in the tuple with the corresponding value in `std::forward<UTypes>(u)`.

**13** *Remarks:* The expression inside `explicit` is equivalent to:

```cpp
!conjunction_v<is_convertible<UTypes, Types>...>
```

```cpp
tuple(const tuple& u) = default;
```

**14** *Mandates:* `is_copy_constructible_v<T_i>` is `true` for all *i*.

**15** *Effects:* Initializes each element of `*this` with the corresponding element of `u`.

```cpp
tuple(tuple&& u) = default;
```

**16** *Constraints:* `is_move_constructible_v<T_i>` is `true` for all *i*.

**17** *Effects:* For all *i*, initializes the *i*<sup></sup>*th* element of `*this` with `std::forward<T_i>(get<i>(u))`.

```cpp
template<class... UTypes> constexpr explicit(see below) tuple(const tuple<UTypes...>& u);
```

**18** *Constraints:*

- `sizeof...(Types)` equals `sizeof...(UTypes)` and
- `is_constructible_v<T_i, const U_i&>` is `true` for all *i*, and
- either `sizeof...(Types)` is not 1, or (when `Types...` expands to `T` and `UTypes...` expands to `U`) `is_convertible_v<const tuple<U>&, T>`, `is_constructible_v<T, const tuple<U>&>`, and `is_same_v<T, U>` are all `false`.

**19** *Effects:* Initializes each element of `*this` with the corresponding element of `u`.

**20** *Remarks:* The expression inside `explicit` is equivalent to:

```cpp
!conjunction_v<is_convertible<const UTypes&, Types>...>
```

```cpp
template<class... UTypes> constexpr explicit(see below) tuple(tuple<UTypes...>&& u);
```

**21** *Constraints:*

- `sizeof...(Types)` equals `sizeof...(UTypes)`, and
- `is_constructible_v<T_i, U_i>` is `true` for all *i*, and
- either `sizeof...(Types)` is not 1, or (when `Types...` expands to `T` and `UTypes...` expands to `U`) `is_convertible_v<tuple<U>, T>`, `is_constructible_v<T, tuple<U>>`, and `is_same_v<T, U>` are all `false`.

**22** *Effects:* For all *i*, initializes the *i*<sup></sup>*th* element of `*this` with `std::forward<U_i>(get<i>(u))`.

**23** *Remarks:* The expression inside `explicit` is equivalent to:

```cpp
!conjunction_v<is_convertible<UTypes, Types>...>
```

```cpp
template<class U1, class U2> constexpr explicit(see below) tuple(const pair<U1, U2>& u);
```

**24** *Constraints:*

- `sizeof...(Types)` is 2,
- `is_constructible_v<T_0, const U1&>` is `true`, and
- `is_constructible_v<T_1, const U2&>` is `true`.

**25** *Effects:* Initializes the first element with `u.first` and the second element with `u.second`.

**26** The expression inside `explicit` is equivalent to:

```cpp
!is_convertible_v<const U1&, T_0> || !is_convertible_v<const U2&, T_1>
```

```cpp
template<class U1, class U2> constexpr explicit(see below) tuple(pair<U1, U2>&& u);
```

**27** *Constraints:*

- `sizeof...(Types)` is 2,
- `is_constructible_v<T_0, U1>` is `true`, and
- `is_constructible_v<T_1, U2>` is `true`.

**28** *Effects:* Initializes the first element with `std::forward<U1>(u.first)` and the second element with `std::forward<U2>(u.second)`.

**29** The expression inside `explicit` is equivalent to:

```cpp
!is_convertible_v<U1, T_0> || !is_convertible_v<U2, T_1>
```

```cpp
template<class Alloc>
  constexpr explicit(see below)
    tuple(allocator_arg_t, const Alloc& a);
template<class Alloc>
  constexpr explicit(see below)
    tuple(allocator_arg_t, const Alloc& a, const Types&...);
template<class Alloc, class... UTypes>
  constexpr explicit(see below)
    tuple(allocator_arg_t, const Alloc& a, UTypes&&...);
template<class Alloc>
  constexpr tuple(allocator_arg_t, const Alloc& a, const tuple&);
template<class Alloc>
  constexpr tuple(allocator_arg_t, const Alloc& a, tuple&&);
template<class Alloc, class... UTypes>
  constexpr explicit(see below)
    tuple(allocator_arg_t, const Alloc& a, const tuple<UTypes...>&);
template<class Alloc, class... UTypes>
  constexpr explicit(see below)
    tuple(allocator_arg_t, const Alloc& a, tuple<UTypes...>&&);
template<class Alloc, class U1, class U2>
  constexpr explicit(see below)
    tuple(allocator_arg_t, const Alloc& a, const pair<U1, U2>&);
template<class Alloc, class U1, class U2>
  constexpr explicit(see below)
    tuple(allocator_arg_t, const Alloc& a, pair<U1, U2>&&);
```

**30** *Preconditions:* `Alloc` meets the *Cpp17Allocator* requirements ([Table 36](../library/allocator.requirements.md#tab:cpp17.allocator)).

**31** *Effects:* Equivalent to the preceding constructors except that each element is constructed with uses-allocator construction ([[allocator.uses.construction]](allocator.uses.construction.md)).
