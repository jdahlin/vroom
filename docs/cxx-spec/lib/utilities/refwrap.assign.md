# 20.14.5.2 Assignment [refwrap.assign]

[Contents](../../README.md) · up: [20.14.5 Class template `reference_wrapper` \[refwrap\]](refwrap.md) · prev: [20.14.5.1 Constructors and destructor \[refwrap.const\]](refwrap.const.md) · next: [20.14.5.3 Access \[refwrap.access\]](refwrap.access.md)

```cpp
constexpr reference_wrapper& operator=(const reference_wrapper& x) noexcept;
```

**1** *Postconditions:* `*this` stores a reference to `x.get()`.
