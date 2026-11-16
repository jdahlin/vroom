# 20.6.3.3 Assignment [optional.assign]

[Contents](../../README.md) · up: [20.6.3 Class template `optional` \[optional.optional\]](optional.optional.md) · prev: [20.6.3.2 Destructor \[optional.dtor\]](optional.dtor.md) · next: [20.6.3.4 Swap \[optional.swap\]](optional.swap.md)

```cpp
optional<T>& operator=(nullopt_t) noexcept;
```

**1** *Effects:* If `*this` contains a value, calls `val->T::~T()` to destroy the contained value; otherwise no effect.

**2** *Postconditions:* `*this` does not contain a value.

**3** *Returns:* `*this`.

```cpp
constexpr optional<T>& operator=(const optional& rhs);
```

**4** *Effects:* See [Table 42](optional.assign.md#tab:optional.assign.copy).

<a id="tab:optional.assign.copy"></a>
**Table 42: `optional::operator=(const optional&)` effects** [tab:optional.assign.copy]

|  |  | `*this` contains a value | `*this` does not contain a value |
| --- | --- | --- | --- |
| `rhs` contains a value |  | assigns `*rhs` to the contained value | initializes the contained value as if direct-non-list-initializing an object of type `T` with `*rhs` |
| `rhs` does not contain a value |  | destroys the contained value by calling `val->T::~T()` | no effect |

**5** *Postconditions:* `bool(rhs) == bool(*this)`.

**6** *Returns:* `*this`.

**7** *Remarks:* If any exception is thrown, the result of the expression `bool(*this)` remains unchanged. If an exception is thrown during the call to `T`’s copy constructor, no effect. If an exception is thrown during the call to `T`’s copy assignment, the state of its contained value is as defined by the exception safety guarantee of `T`’s copy assignment. This operator is defined as deleted unless `is_copy_constructible_v<T>` is `true` and `is_copy_assignable_v<T>` is `true`. If `is_trivially_copy_constructible_v<T> &&` `is_trivially_copy_assignable_v<T> &&` `is_trivially_destructible_v<T>` is `true`, this assignment operator is trivial.

```cpp
constexpr optional& operator=(optional&& rhs) noexcept(see below);
```

**8** *Constraints:* `is_move_constructible_v<T>` is `true` and `is_move_assignable_v<T>` is `true`.

**9** *Effects:* See [Table 43](optional.assign.md#tab:optional.assign.move). The result of the expression `bool(rhs)` remains unchanged.

<a id="tab:optional.assign.move"></a>
**Table 43: `optional::operator=(optional&&)` effects** [tab:optional.assign.move]

|  |  | `*this` contains a value | `*this` does not contain a value |
| --- | --- | --- | --- |
| `rhs` contains a value |  | assigns `std::move(*rhs)` to the contained value | initializes the contained value as if direct-non-list-initializing an object of type `T` with `std::move(*rhs)` |
| `rhs` does not contain a value |  | destroys the contained value by calling `val->T::~T()` | no effect |

**10** *Postconditions:* `bool(rhs) == bool(*this)`.

**11** *Returns:* `*this`.

**12** *Remarks:* The expression inside `noexcept` is equivalent to:

```cpp
is_nothrow_move_assignable_v<T> && is_nothrow_move_constructible_v<T>
```

**13** If any exception is thrown, the result of the expression `bool(*this)` remains unchanged. If an exception is thrown during the call to `T`’s move constructor, the state of `*rhs.val` is determined by the exception safety guarantee of `T`’s move constructor. If an exception is thrown during the call to `T`’s move assignment, the state of `*val` and `*rhs.val` is determined by the exception safety guarantee of `T`’s move assignment. If `is_trivially_move_constructible_v<T> &&` `is_trivially_move_assignable_v<T> &&` `is_trivially_destructible_v<T>` is `true`, this assignment operator is trivial.

```cpp
template<class U = T> optional<T>& operator=(U&& v);
```

**14** *Constraints:* `is_same_v<remove_cvref_t<U>, optional>` is `false`, `conjunction_v<is_scalar<T>, is_same<T, decay_t<U>>>` is `false`, `is_constructible_v<T, U>` is `true`, and `is_assignable_v<T&, U>` is `true`.

**15** *Effects:* If `*this` contains a value, assigns `std::forward<U>(v)` to the contained value; otherwise initializes the contained value as if direct-non-list-initializing object of type `T` with `std::forward<U>(v)`.

**16** *Postconditions:* `*this` contains a value.

**17** *Returns:* `*this`.

**18** *Remarks:* If any exception is thrown, the result of the expression `bool(*this)` remains unchanged. If an exception is thrown during the call to `T`’s constructor, the state of `v` is determined by the exception safety guarantee of `T`’s constructor. If an exception is thrown during the call to `T`’s assignment, the state of `*val` and `v` is determined by the exception safety guarantee of `T`’s assignment.

```cpp
template<class U> optional<T>& operator=(const optional<U>& rhs);
```

**19** *Constraints:*

- `is_constructible_v<T, const U&>` is `true`,
- `is_assignable_v<T&, const U&>` is `true`,
- `is_constructible_v<T, optional<U>&>` is `false`,
- `is_constructible_v<T, optional<U>&&>` is `false`,
- `is_constructible_v<T, const optional<U>&>` is `false`,
- `is_constructible_v<T, const optional<U>&&>` is `false`,
- `is_convertible_v<optional<U>&, T>` is `false`,
- `is_convertible_v<optional<U>&&, T>` is `false`,
- `is_convertible_v<const optional<U>&, T>` is `false`,
- `is_convertible_v<const optional<U>&&, T>` is `false`,
- `is_assignable_v<T&, optional<U>&>` is `false`,
- `is_assignable_v<T&, optional<U>&&>` is `false`,
- `is_assignable_v<T&, const optional<U>&>` is `false`, and
- `is_assignable_v<T&, const optional<U>&&>` is `false`.

**20** *Effects:* See [Table 44](optional.assign.md#tab:optional.assign.copy.templ).

<a id="tab:optional.assign.copy.templ"></a>
**Table 44: `optional::operator=(const optional<U>&)` effects** [tab:optional.assign.copy.templ]

|  |  | `*this` contains a value | `*this` does not contain a value |
| --- | --- | --- | --- |
| `rhs` contains a value |  | assigns `*rhs` to the contained value | initializes the contained value as if direct-non-list-initializing an object of type `T` with `*rhs` |
| `rhs` does not contain a value |  | destroys the contained value by calling `val->T::~T()` | no effect |

**21** *Postconditions:* `bool(rhs) == bool(*this)`.

**22** *Returns:* `*this`.

**23** *Remarks:* If any exception is thrown, the result of the expression `bool(*this)` remains unchanged. If an exception is thrown during the call to `T`’s constructor, the state of `*rhs.val` is determined by the exception safety guarantee of `T`’s constructor. If an exception is thrown during the call to `T`’s assignment, the state of `*val` and `*rhs.val` is determined by the exception safety guarantee of `T`’s assignment.

```cpp
template<class U> optional<T>& operator=(optional<U>&& rhs);
```

**24** *Constraints:*

- `is_constructible_v<T, U>` is `true`,
- `is_assignable_v<T&, U>` is `true`,
- `is_constructible_v<T, optional<U>&>` is `false`,
- `is_constructible_v<T, optional<U>&&>` is `false`,
- `is_constructible_v<T, const optional<U>&>` is `false`,
- `is_constructible_v<T, const optional<U>&&>` is `false`,
- `is_convertible_v<optional<U>&, T>` is `false`,
- `is_convertible_v<optional<U>&&, T>` is `false`,
- `is_convertible_v<const optional<U>&, T>` is `false`,
- `is_convertible_v<const optional<U>&&, T>` is `false`,
- `is_assignable_v<T&, optional<U>&>` is `false`,
- `is_assignable_v<T&, optional<U>&&>` is `false`,
- `is_assignable_v<T&, const optional<U>&>` is `false`, and
- `is_assignable_v<T&, const optional<U>&&>` is `false`.

**25** *Effects:* See [Table 45](optional.assign.md#tab:optional.assign.move.templ). The result of the expression `bool(rhs)` remains unchanged.

<a id="tab:optional.assign.move.templ"></a>
**Table 45: `optional::operator=(optional<U>&&)` effects** [tab:optional.assign.move.templ]

|  |  | `*this` contains a value | `*this` does not contain a value |
| --- | --- | --- | --- |
| `rhs` contains a value |  | assigns `std::move(*rhs)` to the contained value | initializes the contained value as if direct-non-list-initializing an object of type `T` with `std::move(*rhs)` |
| `rhs` does not contain a value |  | destroys the contained value by calling `val->T::~T()` | no effect |

**26** *Postconditions:* `bool(rhs) == bool(*this)`.

**27** *Returns:* `*this`.

**28** *Remarks:* If any exception is thrown, the result of the expression `bool(*this)` remains unchanged. If an exception is thrown during the call to `T`’s constructor, the state of `*rhs.val` is determined by the exception safety guarantee of `T`’s constructor. If an exception is thrown during the call to `T`’s assignment, the state of `*val` and `*rhs.val` is determined by the exception safety guarantee of `T`’s assignment.

```cpp
template<class... Args> T& emplace(Args&&... args);
```

**29** *Mandates:* `is_constructible_v<T, Args...>` is `true`.

**30** *Effects:* Calls `*this = nullopt`. Then initializes the contained value as if direct-non-list-initializing an object of type `T` with the arguments `std::forward<Args>(args)...`.

**31** *Postconditions:* `*this` contains a value.

**32** *Returns:* A reference to the new contained value.

**33** *Throws:* Any exception thrown by the selected constructor of `T`.

**34** *Remarks:* If an exception is thrown during the call to `T`’s constructor, `*this` does not contain a value, and the previous `*val` (if any) has been destroyed.

```cpp
template<class U, class... Args> T& emplace(initializer_list<U> il, Args&&... args);
```

**35** *Constraints:* `is_constructible_v<T, initializer_list<U>&, Args...>` is `true`.

**36** *Effects:* Calls `*this = nullopt`. Then initializes the contained value as if direct-non-list-initializing an object of type `T` with the arguments `il, std::forward<Args>(args)...`.

**37** *Postconditions:* `*this` contains a value.

**38** *Returns:* A reference to the new contained value.

**39** *Throws:* Any exception thrown by the selected constructor of `T`.

**40** *Remarks:* If an exception is thrown during the call to `T`’s constructor, `*this` does not contain a value, and the previous `*val` (if any) has been destroyed.
