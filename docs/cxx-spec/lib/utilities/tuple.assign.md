# 20.5.3.2 Assignment [tuple.assign]

[Contents](../../README.md) · up: [20.5.3 Class template `tuple` \[tuple.tuple\]](tuple.tuple.md) · prev: [20.5.3.1 Construction \[tuple.cnstr\]](tuple.cnstr.md) · next: [20.5.3.3 `swap` \[tuple.swap\]](tuple.swap.md)

**1** For each `tuple` assignment operator, an exception is thrown only if the assignment of one of the types in `Types` throws an exception. In the function descriptions that follow, let *i* be in the range `[0, sizeof...(Types))` in order, `T`<sub>*i*</sub> be the *i*<sup></sup>*th* type in `Types`, and `U`<sub>*i*</sub> be the *i*<sup></sup>*th* type in a template parameter pack named `UTypes`, where indexing is zero-based.

```cpp
constexpr tuple& operator=(const tuple& u);
```

**2** *Effects:* Assigns each element of `u` to the corresponding element of `*this`.

**3** *Remarks:* This operator is defined as deleted unless `is_copy_assignable_v<T_i>` is `true` for all *i*.

**4** *Returns:* `*this`.

```cpp
constexpr tuple& operator=(tuple&& u) noexcept(see below);
```

**5** *Constraints:* `is_move_assignable_v<T_i>` is `true` for all *i*.

**6** *Effects:* For all *i*, assigns `std::forward<T_i>(get<i>(u))` to `get<i>(*this)`.

**7** *Remarks:* The expression inside `noexcept` is equivalent to the logical and of the following expressions:

```cpp
is_nothrow_move_assignable_v<T_i>
```

where `T`<sub>*i*</sub> is the *i*<sup></sup>*th* type in `Types`.

**8** *Returns:* `*this`.

```cpp
template<class... UTypes> constexpr tuple& operator=(const tuple<UTypes...>& u);
```

**9** *Constraints:*

- `sizeof...(Types)` equals `sizeof...(UTypes)` and
- `is_assignable_v<T_i&, const U_i&>` is `true` for all *i*.

**10** *Effects:* Assigns each element of `u` to the corresponding element of `*this`.

**11** *Returns:* `*this`.

```cpp
template<class... UTypes> constexpr tuple& operator=(tuple<UTypes...>&& u);
```

**12** *Constraints:*

- `sizeof...(Types)` equals `sizeof...(UTypes)` and
- `is_assignable_v<T_i&, U_i>` is `true` for all *i*.

**13** *Effects:* For all *i*, assigns `std::forward<U_i>(get<i>(u))` to `get<i>(*this)`.

**14** *Returns:* `*this`.

```cpp
template<class U1, class U2> constexpr tuple& operator=(const pair<U1, U2>& u);
```

**15** *Constraints:*

- `sizeof...(Types)` is 2 and
- `is_assignable_v<T_0&, const U1&>` is `true`, and
- `is_assignable_v<T_1&, const U2&>` is `true`.

**16** *Effects:* Assigns `u.first` to the first element of `*this` and `u.second` to the second element of `*this`.

**17** *Returns:* `*this`.

```cpp
template<class U1, class U2> constexpr tuple& operator=(pair<U1, U2>&& u);
```

**18** *Constraints:*

- `sizeof...(Types)` is 2 and
- `is_assignable_v<T_0&, U1>` is `true`, and
- `is_assignable_v<T_1&, U2>` is `true`.

**19** *Effects:* Assigns `std::forward<U1>(u.first)` to the first element of `*this` and<br> `std::forward<U2>(u.second)` to the second element of `*this`.

**20** *Returns:* `*this`.
