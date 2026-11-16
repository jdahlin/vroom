# 20.14.5.5 Helper functions [refwrap.helpers]

[Contents](../README.md) · up: [20.14.5 Class template `reference_wrapper` \[refwrap\]](refwrap.md) · prev: [20.14.5.4 Invocation \[refwrap.invoke\]](refwrap.invoke.md) · next: [20.14.6 Arithmetic operations \[arithmetic.operations\]](arithmetic.operations.md)

**1** The template parameter `T` of the following `ref` and `cref` function templates may be an incomplete type.

```cpp
template<class T> constexpr reference_wrapper<T> ref(T& t) noexcept;
```

**2** *Returns:* `reference_wrapper<T>(t)`.

```cpp
template<class T> constexpr reference_wrapper<T> ref(reference_wrapper<T> t) noexcept;
```

**3** *Returns:* `ref(t.get())`.

```cpp
template<class T> constexpr reference_wrapper<const T> cref(const T& t) noexcept;
```

**4** *Returns:* `reference_wrapper <const T>(t)`.

```cpp
template<class T> constexpr reference_wrapper<const T> cref(reference_wrapper<T> t) noexcept;
```

**5** *Returns:* `cref(t.get())`.
