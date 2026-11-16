# 20.7.3.1 Constructors [variant.ctor]

[Contents](../../README.md) · up: [20.7.3 Class template `variant` \[variant.variant\]](variant.variant.md) · prev: [20.7.3 Class template `variant` \[variant.variant\]](variant.variant.md) · next: [20.7.3.2 Destructor \[variant.dtor\]](variant.dtor.md)

**1** In the descriptions that follow, let *i* be in the range `[0, sizeof...(Types))`, and `T`<sub>*i*</sub> be the *i*<sup></sup>*th* type in `Types`.

```cpp
constexpr variant() noexcept(see below);
```

**2** *Constraints:* `is_default_constructible_v<T_0>` is `true`.

**3** *Effects:* Constructs a `variant` holding a value-initialized value of type `T`<sub>0</sub>.

**4** *Postconditions:* `valueless_by_exception()` is `false` and `index()` is `0`.

**5** *Throws:* Any exception thrown by the value-initialization of `T`<sub>0</sub>.

**6** *Remarks:* This function is `constexpr` if and only if the value-initialization of the alternative type `T`<sub>0</sub> would satisfy the requirements for a constexpr function. The expression inside `noexcept` is equivalent to `is_nothrow_default_constructible_v<T_0>`. [*Note 1*: See also class `monostate`. — *end note*]

```cpp
constexpr variant(const variant& w);
```

**7** *Effects:* If `w` holds a value, initializes the `variant` to hold the same alternative as `w` and direct-initializes the contained value with `get<j>(w)`, where `j` is `w.index()`. Otherwise, initializes the `variant` to not hold a value.

**8** *Throws:* Any exception thrown by direct-initializing any `T`<sub>*i*</sub> for all *i*.

**9** *Remarks:* This constructor is defined as deleted unless `is_copy_constructible_v<T_i>` is `true` for all *i*. If `is_trivially_copy_constructible_v<T_i>` is `true` for all *i*, this constructor is trivial.

```cpp
constexpr variant(variant&& w) noexcept(see below);
```

**10** *Constraints:* `is_move_constructible_v<T_i>` is `true` for all *i*.

**11** *Effects:* If `w` holds a value, initializes the `variant` to hold the same alternative as `w` and direct-initializes the contained value with `get<j>(std::move(w))`, where `j` is `w.index()`. Otherwise, initializes the `variant` to not hold a value.

**12** *Throws:* Any exception thrown by move-constructing any `T`<sub>*i*</sub> for all *i*.

**13** *Remarks:* The expression inside `noexcept` is equivalent to the logical AND of `is_nothrow_move_constructible_v<T_i>` for all *i*. If `is_trivially_move_constructible_v<T_i>` is `true` for all *i*, this constructor is trivial.

```cpp
template<class T> constexpr variant(T&& t) noexcept(see below);
```

**14** Let `T`<sub>*j*</sub> be a type that is determined as follows: build an imaginary function `FUN(T_i)` for each alternative type `T`<sub>*i*</sub> for which `T_i x[] =` `{std::forward<T>(t)};` is well-formed for some invented variable `x`. The overload `FUN(T_j)` selected by overload resolution for the expression `FUN(std::forward<T>(t))` defines the alternative `T`<sub>*j*</sub> which is the type of the contained value after construction.

**15** *Constraints:*

- `sizeof...(Types)` is nonzero,

- `is_same_v<remove_cvref_t<T>, variant>` is `false`,

- `remove_cvref_t<T>` is neither a specialization of `in_place_type_t` nor a specialization of `in_place_index_t`,

- `is_constructible_v<T_j, T>` is `true`, and

- the expression `FUN(``std::forward<T>(t))` (with `FUN` being the above-mentioned set of imaginary functions) is well-formed. [*Note 2*:

  ```cpp
  variant<string, string> v("abc");
  ```

  is ill-formed, as both alternative types have an equally viable constructor for the argument. — *end note*]

**16** *Effects:* Initializes `*this` to hold the alternative type `T`<sub>*j*</sub> and direct-initializes the contained value as if direct-non-list-initializing it with `std::forward<T>(t)`.

**17** *Postconditions:* `holds_alternative<T_j>(*this)` is `true`.

**18** *Throws:* Any exception thrown by the initialization of the selected alternative `T`<sub>*j*</sub>.

**19** *Remarks:* The expression inside `noexcept` is equivalent to `is_nothrow_constructible_v<T_j, T>`. If `T`<sub>*j*</sub>’s selected constructor is a constexpr constructor, this constructor is a constexpr constructor.

```cpp
template<class T, class... Args> constexpr explicit variant(in_place_type_t<T>, Args&&... args);
```

**20** *Constraints:*

- There is exactly one occurrence of `T` in `Types...` and
- `is_constructible_v<T, Args...>` is `true`.

**21** *Effects:* Initializes the contained value as if direct-non-list-initializing an object of type `T` with the arguments `std::forward<Args>(args)...`.

**22** *Postconditions:* `holds_alternative<T>(*this)` is `true`.

**23** *Throws:* Any exception thrown by calling the selected constructor of `T`.

**24** *Remarks:* If `T`’s selected constructor is a constexpr constructor, this constructor is a constexpr constructor.

```cpp
template<class T, class U, class... Args>
  constexpr explicit variant(in_place_type_t<T>, initializer_list<U> il, Args&&... args);
```

**25** *Constraints:*

- There is exactly one occurrence of `T` in `Types...` and
- `is_constructible_v<T, initializer_list<U>&, Args...>` is `true`.

**26** *Effects:* Initializes the contained value as if direct-non-list-initializing an object of type `T` with the arguments `il, std::forward<Args>(args)...`.

**27** *Postconditions:* `holds_alternative<T>(*this)` is `true`.

**28** *Throws:* Any exception thrown by calling the selected constructor of `T`.

**29** *Remarks:* If `T`’s selected constructor is a constexpr constructor, this constructor is a constexpr constructor.

```cpp
template<size_t I, class... Args> constexpr explicit variant(in_place_index_t<I>, Args&&... args);
```

**30** *Constraints:*

- `I` is less than `sizeof...(Types)` and
- `is_constructible_v<T_I, Args...>` is `true`.

**31** *Effects:* Initializes the contained value as if direct-non-list-initializing an object of type `T`<sub>*I*</sub> with the arguments `std::forward<Args>(args)...`.

**32** *Postconditions:* `index()` is `I`.

**33** *Throws:* Any exception thrown by calling the selected constructor of `T`<sub>*I*</sub>.

**34** *Remarks:* If `T`<sub>*I*</sub>’s selected constructor is a constexpr constructor, this constructor is a constexpr constructor.

```cpp
template<size_t I, class U, class... Args>
  constexpr explicit variant(in_place_index_t<I>, initializer_list<U> il, Args&&... args);
```

**35** *Constraints:*

- `I` is less than `sizeof...(Types)` and
- `is_constructible_v<T_I, initializer_list<U>&, Args...>` is `true`.

**36** *Effects:* Initializes the contained value as if direct-non-list-initializing an object of type `T`<sub>*I*</sub> with the arguments `il, std::forward<Args>(args)...`.

**37** *Postconditions:* `index()` is `I`.

**38** *Remarks:* If `T`<sub>*I*</sub>’s selected constructor is a constexpr constructor, this constructor is a constexpr constructor.
