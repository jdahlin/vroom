# 20.6.3.1 Constructors [optional.ctor]

[Contents](../README.md) · up: [20.6.3 Class template `optional` \[optional.optional\]](optional.optional.md) · prev: [20.6.3 Class template `optional` \[optional.optional\]](optional.optional.md) · next: [20.6.3.2 Destructor \[optional.dtor\]](optional.dtor.md)

```cpp
constexpr optional() noexcept;
constexpr optional(nullopt_t) noexcept;
```

**1** *Postconditions:* `*this` does not contain a value.

**2** *Remarks:* No contained value is initialized. For every object type `T` these constructors are constexpr constructors ([[dcl.constexpr]](../dcl.constexpr.md)).

```cpp
constexpr optional(const optional& rhs);
```

**3** *Effects:* If `rhs` contains a value, initializes the contained value as if direct-non-list-initializing an object of type `T` with the expression `*rhs`.

**4** *Postconditions:* `bool(rhs) == bool(*this)`.

**5** *Throws:* Any exception thrown by the selected constructor of `T`.

**6** *Remarks:* This constructor is defined as deleted unless `is_copy_constructible_v<T>` is `true`. If `is_trivially_copy_constructible_v<T>` is `true`, this constructor is trivial.

```cpp
constexpr optional(optional&& rhs) noexcept(see below);
```

**7** *Constraints:* `is_move_constructible_v<T>` is `true`.

**8** *Effects:* If `rhs` contains a value, initializes the contained value as if direct-non-list-initializing an object of type `T` with the expression `std::move(*rhs)`. `bool(rhs)` is unchanged.

**9** *Postconditions:* `bool(rhs) == bool(*this)`.

**10** *Throws:* Any exception thrown by the selected constructor of `T`.

**11** *Remarks:* The expression inside `noexcept` is equivalent to `is_nothrow_move_constructible_v<T>`. If `is_trivially_move_constructible_v<T>` is `true`, this constructor is trivial.

```cpp
template<class... Args> constexpr explicit optional(in_place_t, Args&&... args);
```

**12** *Constraints:* `is_constructible_v<T, Args...>` is `true`.

**13** *Effects:* Initializes the contained value as if direct-non-list-initializing an object of type `T` with the arguments `std::forward<Args>(args)...`.

**14** *Postconditions:* `*this` contains a value.

**15** *Throws:* Any exception thrown by the selected constructor of `T`.

**16** *Remarks:* If `T`’s constructor selected for the initialization is a constexpr constructor, this constructor is a constexpr constructor.

```cpp
template<class U, class... Args>
  constexpr explicit optional(in_place_t, initializer_list<U> il, Args&&... args);
```

**17** *Constraints:* `is_constructible_v<T, initializer_list<U>&, Args...>` is `true`.

**18** *Effects:* Initializes the contained value as if direct-non-list-initializing an object of type `T` with the arguments `il, std::forward<Args>(args)...`.

**19** *Postconditions:* `*this` contains a value.

**20** *Throws:* Any exception thrown by the selected constructor of `T`.

**21** *Remarks:* If `T`’s constructor selected for the initialization is a constexpr constructor, this constructor is a constexpr constructor.

```cpp
template<class U = T> constexpr explicit(see below) optional(U&& v);
```

**22** *Constraints:* `is_constructible_v<T, U>` is `true`, `is_same_v<remove_cvref_t<U>, in_place_t>` is `false`, and `is_same_v<remove_cvref_t<U>, optional>` is `false`.

**23** *Effects:* Initializes the contained value as if direct-non-list-initializing an object of type `T` with the expression `std::forward<U>(v)`.

**24** *Postconditions:* `*this` contains a value.

**25** *Throws:* Any exception thrown by the selected constructor of `T`.

**26** *Remarks:* If `T`’s selected constructor is a constexpr constructor, this constructor is a constexpr constructor. The expression inside `explicit` is equivalent to:

```cpp
!is_convertible_v<U, T>
```

```cpp
template<class U> explicit(see below) optional(const optional<U>& rhs);
```

**27** *Constraints:*

- `is_constructible_v<T, const U&>` is `true`,
- `is_constructible_v<T, optional<U>&>` is `false`,
- `is_constructible_v<T, optional<U>&&>` is `false`,
- `is_constructible_v<T, const optional<U>&>` is `false`,
- `is_constructible_v<T, const optional<U>&&>` is `false`,
- `is_convertible_v<optional<U>&, T>` is `false`,
- `is_convertible_v<optional<U>&&, T>` is `false`,
- `is_convertible_v<const optional<U>&, T>` is `false`, and
- `is_convertible_v<const optional<U>&&, T>` is `false`.

**28** *Effects:* If `rhs` contains a value, initializes the contained value as if direct-non-list-initializing an object of type `T` with the expression `*rhs`.

**29** *Postconditions:* `bool(rhs)` == `bool(*this)`.

**30** *Throws:* Any exception thrown by the selected constructor of `T`.

**31** *Remarks:* The expression inside `explicit` is equivalent to:

```cpp
!is_convertible_v<const U&, T>
```

```cpp
template<class U> explicit(see below) optional(optional<U>&& rhs);
```

**32** *Constraints:*

- `is_constructible_v<T, U>` is true,
- `is_constructible_v<T, optional<U>&>` is `false`,
- `is_constructible_v<T, optional<U>&&>` is `false`,
- `is_constructible_v<T, const optional<U>&>` is `false`,
- `is_constructible_v<T, const optional<U>&&>` is `false`,
- `is_convertible_v<optional<U>&, T>` is `false`,
- `is_convertible_v<optional<U>&&, T>` is `false`,
- `is_convertible_v<const optional<U>&, T>` is `false`, and
- `is_convertible_v<const optional<U>&&, T>` is `false`.

**33** *Effects:* If `rhs` contains a value, initializes the contained value as if direct-non-list-initializing an object of type `T` with the expression `std::move(*rhs)`. `bool(rhs)` is unchanged.

**34** *Postconditions:* `bool(rhs)` == `bool(*this)`.

**35** *Throws:* Any exception thrown by the selected constructor of `T`.

**36** *Remarks:* The expression inside `explicit` is equivalent to:

```cpp
!is_convertible_v<U, T>
```
