# 20.7.3.3 Assignment [variant.assign]

[Contents](../../README.md) · up: [20.7.3 Class template `variant` \[variant.variant\]](variant.variant.md) · prev: [20.7.3.2 Destructor \[variant.dtor\]](variant.dtor.md) · next: [20.7.3.4 Modifiers \[variant.mod\]](variant.mod.md)

```cpp
constexpr variant& operator=(const variant& rhs);
```

**1** Let *j* be `rhs.index()`.

**2** *Effects:*

- If neither `*this` nor `rhs` holds a value, there is no effect.
- Otherwise, if `*this` holds a value but `rhs` does not, destroys the value contained in `*this` and sets `*this` to not hold a value.
- Otherwise, if `index() == j`, assigns the value contained in `rhs` to the value contained in `*this`.
- Otherwise, if either `is_nothrow_copy_constructible_v<T_j>` is `true` or `is_nothrow_move_constructible_v<T_j>` is `false`, equivalent to `emplace<j>(get<j>(rhs))`.
- Otherwise, equivalent to `operator=(variant(rhs))`.

**3** *Returns:* `*this`.

**4** *Postconditions:* `index() == rhs.index()`.

**5** *Remarks:* This operator is defined as deleted unless `is_copy_constructible_v<T_i> &&` `is_copy_assignable_v<T_i>` is `true` for all *i*. If `is_trivially_copy_constructible_v<T_i> &&` `is_trivially_copy_assignable_v<T_i> &&` `is_trivially_destructible_v<T_i>` is `true` for all *i*, this assignment operator is trivial.

```cpp
constexpr variant& operator=(variant&& rhs) noexcept(see below);
```

**6** Let *j* be `rhs.index()`.

**7** *Constraints:* `is_move_constructible_v<T_i> &&` `is_move_assignable_v<T_i>` is `true` for all *i*.

**8** *Effects:*

- If neither `*this` nor `rhs` holds a value, there is no effect.
- Otherwise, if `*this` holds a value but `rhs` does not, destroys the value contained in `*this` and sets `*this` to not hold a value.
- Otherwise, if `index() == j`, assigns `get<j>(std::move(rhs))` to the value contained in `*this`.
- Otherwise, equivalent to `emplace<j>(get<j>(std::move(rhs)))`.

**9** *Returns:* `*this`.

**10** *Remarks:* If `is_trivially_move_constructible_v<T_i> &&` `is_trivially_move_assignable_v<T_i> &&` `is_trivially_destructible_v<T_i>` is `true` for all *i*, this assignment operator is trivial. The expression inside `noexcept` is equivalent to: `is_nothrow_move_constructible_v<T_i> && is_nothrow_move_assignable_v<T_i>` for all *i*.

- If an exception is thrown during the call to `T`<sub>*j*</sub>’s move construction (with *j* being `rhs.index()`), the `variant` will hold no value.
- If an exception is thrown during the call to `T`<sub>*j*</sub>’s move assignment, the state of the contained value is as defined by the exception safety guarantee of `T`<sub>*j*</sub>’s move assignment; `index()` will be *j*.

```cpp
template<class T> variant& operator=(T&& t) noexcept(see below);
```

**11** Let `T`<sub>*j*</sub> be a type that is determined as follows: build an imaginary function `FUN(T_i)` for each alternative type `T`<sub>*i*</sub> for which `T_i x[] =` `{std::forward<T>(t)};` is well-formed for some invented variable `x`. The overload `FUN(T_j)` selected by overload resolution for the expression `FUN(std::forward<T>(t))` defines the alternative `T`<sub>*j*</sub> which is the type of the contained value after assignment.

**12** *Constraints:*

- `is_same_v<remove_cvref_t<T>, variant>` is `false`,

- `is_assignable_v<T_j&, T> && is_constructible_v<T_j, T>` is `true`, and

- the expression `FUN(std::forward<T>(t))` (with `FUN` being the above-mentioned set of imaginary functions) is well-formed. [*Note 1*:

  ```cpp
  variant<string, string> v;
  v = "abc";
  ```

  is ill-formed, as both alternative types have an equally viable constructor for the argument. — *end note*]

**13** *Effects:*

- If `*this` holds a `T`<sub>*j*</sub>, assigns `std::forward<T>(t)` to the value contained in `*this`.
- Otherwise, if `is_nothrow_constructible_v<T_j, T> ||` `!is_nothrow_move_constructible_v<T_j>` is `true`, equivalent to `emplace<j>(std::forward<T>(t))`.
- Otherwise, equivalent to `operator=(variant(std::forward<T>(t)))`.

**14** *Postconditions:* `holds_alternative<T_j>(*this)` is `true`, with `T`<sub>*j*</sub> selected by the imaginary function overload resolution described above.

**15** *Returns:* `*this`.

**16** *Remarks:* The expression inside `noexcept` is equivalent to:

```cpp
is_nothrow_assignable_v<T_j&, T> && is_nothrow_constructible_v<T_j, T>
```

- If an exception is thrown during the assignment of `std::forward<T>(t)` to the value contained in `*this`, the state of the contained value and `t` are as defined by the exception safety guarantee of the assignment expression; `valueless_by_exception()` will be `false`.
- If an exception is thrown during the initialization of the contained value, the `variant` object might not hold a value.
