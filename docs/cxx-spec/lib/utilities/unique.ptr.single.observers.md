# 20.11.1.2.4 Observers [unique.ptr.single.observers]

[Contents](../../README.md) · up: [20.11.1.2 `unique_ptr` for single objects \[unique.ptr.single\]](unique.ptr.single.md) · prev: [20.11.1.2.3 Assignment \[unique.ptr.single.asgn\]](unique.ptr.single.asgn.md) · next: [20.11.1.2.5 Modifiers \[unique.ptr.single.modifiers\]](unique.ptr.single.modifiers.md)

```cpp
add_lvalue_reference_t<T> operator*() const;
```

**1** *Preconditions:* `get() != nullptr`.

**2** *Returns:* `*get()`.

```cpp
pointer operator->() const noexcept;
```

**3** *Preconditions:* `get() != nullptr`.

**4** *Returns:* `get()`.

**5** [*Note 1*: The use of this function typically requires that `T` be a complete type. — *end note*]

```cpp
pointer get() const noexcept;
```

**6** *Returns:* The stored pointer.

```cpp
deleter_type& get_deleter() noexcept;
const deleter_type& get_deleter() const noexcept;
```

**7** *Returns:* A reference to the stored deleter.

```cpp
explicit operator bool() const noexcept;
```

**8** *Returns:* `get() != nullptr`.
