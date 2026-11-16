# 20.6.3.5 Observers [optional.observe]

[Contents](../README.md) · up: [20.6.3 Class template `optional` \[optional.optional\]](optional.optional.md) · prev: [20.6.3.4 Swap \[optional.swap\]](optional.swap.md) · next: [20.6.3.6 Modifiers \[optional.mod\]](optional.mod.md)

```cpp
constexpr const T* operator->() const;
constexpr T* operator->();
```

**1** *Preconditions:* `*this` contains a value.

**2** *Returns:* `val`.

**3** *Throws:* Nothing.

**4** *Remarks:* These functions are constexpr functions.

```cpp
constexpr const T& operator*() const&;
constexpr T& operator*() &;
```

**5** *Preconditions:* `*this` contains a value.

**6** *Returns:* `*val`.

**7** *Throws:* Nothing.

**8** *Remarks:* These functions are constexpr functions.

```cpp
constexpr T&& operator*() &&;
constexpr const T&& operator*() const&&;
```

**9** *Preconditions:* `*this` contains a value.

**10** *Effects:* Equivalent to: `return std::move(*val);`

```cpp
constexpr explicit operator bool() const noexcept;
```

**11** *Returns:* `true` if and only if `*this` contains a value.

**12** *Remarks:* This function is a constexpr function.

```cpp
constexpr bool has_value() const noexcept;
```

**13** *Returns:* `true` if and only if `*this` contains a value.

**14** *Remarks:* This function is a constexpr function.

```cpp
constexpr const T& value() const&;
constexpr T& value() &;
```

**15** *Effects:* Equivalent to:

```cpp
return bool(*this) ? *val : throw bad_optional_access();
```

```cpp
constexpr T&& value() &&;
constexpr const T&& value() const&&;
```

**16** *Effects:* Equivalent to:

```cpp
return bool(*this) ? std::move(*val) : throw bad_optional_access();
```

```cpp
template<class U> constexpr T value_or(U&& v) const&;
```

**17** *Mandates:* `is_copy_constructible_v<T> && is_convertible_v<U&&, T>` is `true`.

**18** *Effects:* Equivalent to:

```cpp
return bool(*this) ? **this : static_cast<T>(std::forward<U>(v));
```

```cpp
template<class U> constexpr T value_or(U&& v) &&;
```

**19** *Mandates:* `is_move_constructible_v<T> && is_convertible_v<U&&, T>` is `true`.

**20** *Effects:* Equivalent to:

```cpp
return bool(*this) ? std::move(**this) : static_cast<T>(std::forward<U>(v));
```
