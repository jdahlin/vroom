# 31.7.5 Member operators common to integers and pointers to objects [atomics.ref.memop]

[Contents](../README.md) · up: [31.7 Class template `atomic_ref` \[atomics.ref.generic\]](atomics.ref.generic.md) · prev: [31.7.4 Partial specialization for pointers \[atomics.ref.pointer\]](atomics.ref.pointer.md) · next: [31.8 Class template `atomic` \[atomics.types.generic\]](atomics.types.generic.md)

```cpp
value_type operator++(int) const noexcept;
```

**1** *Effects:* Equivalent to: `return fetch_add(1);`

```cpp
value_type operator--(int) const noexcept;
```

**2** *Effects:* Equivalent to: `return fetch_sub(1);`

```cpp
value_type operator++() const noexcept;
```

**3** *Effects:* Equivalent to: `return fetch_add(1) + 1;`

```cpp
value_type operator--() const noexcept;
```

**4** *Effects:* Equivalent to: `return fetch_sub(1) - 1;`
