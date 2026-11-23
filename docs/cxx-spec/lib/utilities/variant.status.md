# 20.7.3.5 Value status [variant.status]

[Contents](../../README.md) · up: [20.7.3 Class template `variant` \[variant.variant\]](variant.variant.md) · prev: [20.7.3.4 Modifiers \[variant.mod\]](variant.mod.md) · next: [20.7.3.6 Swap \[variant.swap\]](variant.swap.md)

```cpp
constexpr bool valueless_by_exception() const noexcept;
```

**1** *Effects:* Returns `false` if and only if the `variant` holds a value.

**2** [*Note 1*: A `variant` might not hold a value if an exception is thrown during a type-changing assignment or emplacement. The latter means that even a `variant<float, int>` can become `valueless_by_exception()`, for instance by

```cpp
struct S { operator int() { throw 42; }};
variant<float, int> v{12.f};
v.emplace<1>(S());
```

— *end note*]

```cpp
constexpr size_t index() const noexcept;
```

**3** *Effects:* If `valueless_by_exception()` is `true`, returns `variant_npos`. Otherwise, returns the zero-based index of the alternative of the contained value.
