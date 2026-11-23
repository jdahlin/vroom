# 20.11.1.3.3 Observers [unique.ptr.runtime.observers]

[Contents](../../README.md) · up: [20.11.1.3 `unique_ptr` for array objects with a runtime length \[unique.ptr.runtime\]](unique.ptr.runtime.md) · prev: [20.11.1.3.2 Assignment \[unique.ptr.runtime.asgn\]](unique.ptr.runtime.asgn.md) · next: [20.11.1.3.4 Modifiers \[unique.ptr.runtime.modifiers\]](unique.ptr.runtime.modifiers.md)

```cpp
T& operator[](size_t i) const;
```

**1** *Preconditions:* `i` &lt; the number of elements in the array to which the stored pointer points.

**2** *Returns:* `get()[i]`.
