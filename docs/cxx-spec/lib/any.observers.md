# 20.8.3.4 Observers [any.observers]

[Contents](../README.md) · up: [20.8.3 Class `any` \[any.class\]](any.class.md) · prev: [20.8.3.3 Modifiers \[any.modifiers\]](any.modifiers.md) · next: [20.8.4 Non-member functions \[any.nonmembers\]](any.nonmembers.md)

```cpp
bool has_value() const noexcept;
```

**1** *Returns:* `true` if `*this` contains an object, otherwise `false`.

```cpp
const type_info& type() const noexcept;
```

**2** *Returns:* `typeid(T)` if `*this` has a contained value of type `T`, otherwise `typeid(void)`.

**3** [*Note 1*: Useful for querying against types known either at compile time or only at runtime. — *end note*]
