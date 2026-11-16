# 20.11.1.3.4 Modifiers [unique.ptr.runtime.modifiers]

[Contents](../../README.md) · up: [20.11.1.3 `unique_ptr` for array objects with a runtime length \[unique.ptr.runtime\]](unique.ptr.runtime.md) · prev: [20.11.1.3.3 Observers \[unique.ptr.runtime.observers\]](unique.ptr.runtime.observers.md) · next: [20.11.1.4 Creation \[unique.ptr.create\]](unique.ptr.create.md)

```cpp
void reset(nullptr_t p = nullptr) noexcept;
```

**1** *Effects:* Equivalent to `reset(pointer())`.

```cpp
template<class U> void reset(U p) noexcept;
```

**2** This function behaves the same as the `reset` member of the primary template.

**3** *Constraints:*

- `U` is the same type as `pointer`, or
- `pointer` is the same type as `element_type*`, `U` is a pointer type `V*`, and `V(*)[]` is convertible to `element_type(*)[]`.
