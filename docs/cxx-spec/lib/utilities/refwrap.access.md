# 20.14.5.3 Access [refwrap.access]

[Contents](../../README.md) · up: [20.14.5 Class template `reference_wrapper` \[refwrap\]](refwrap.md) · prev: [20.14.5.2 Assignment \[refwrap.assign\]](refwrap.assign.md) · next: [20.14.5.4 Invocation \[refwrap.invoke\]](refwrap.invoke.md)

```cpp
constexpr operator T& () const noexcept;
```

**1** *Returns:* The stored reference.

```cpp
constexpr T& get() const noexcept;
```

**2** *Returns:* The stored reference.
