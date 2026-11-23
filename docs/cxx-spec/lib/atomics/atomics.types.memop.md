# 31.8.5 Member operators common to integers and pointers to objects [atomics.types.memop]

[Contents](../../README.md) · up: [31.8 Class template `atomic` \[atomics.types.generic\]](atomics.types.generic.md) · prev: [31.8.4 Partial specialization for pointers \[atomics.types.pointer\]](atomics.types.pointer.md) · next: [31.8.6 Partial specializations for smart pointers \[util.smartptr.atomic\]](util.smartptr.atomic.md)

```cpp
value_type operator++(int) volatile noexcept;
value_type operator++(int) noexcept;
```

**1** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**2** *Effects:* Equivalent to: `return fetch_add(1);`

```cpp
value_type operator--(int) volatile noexcept;
value_type operator--(int) noexcept;
```

**3** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**4** *Effects:* Equivalent to: `return fetch_sub(1);`

```cpp
value_type operator++() volatile noexcept;
value_type operator++() noexcept;
```

**5** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**6** *Effects:* Equivalent to: `return fetch_add(1) + 1;`

```cpp
value_type operator--() volatile noexcept;
value_type operator--() noexcept;
```

**7** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**8** *Effects:* Equivalent to: `return fetch_sub(1) - 1;`
