# 20.11.1.2.5 Modifiers [unique.ptr.single.modifiers]

[Contents](../README.md) · up: [20.11.1.2 `unique_ptr` for single objects \[unique.ptr.single\]](unique.ptr.single.md) · prev: [20.11.1.2.4 Observers \[unique.ptr.single.observers\]](unique.ptr.single.observers.md) · next: [20.11.1.3 `unique_ptr` for array objects with a runtime length \[unique.ptr.runtime\]](unique.ptr.runtime.md)

```cpp
pointer release() noexcept;
```

**1** *Postconditions:* `get() == nullptr`.

**2** *Returns:* The value `get()` had at the start of the call to `release`.

```cpp
void reset(pointer p = pointer()) noexcept;
```

**3** *Preconditions:* The expression `get_deleter()(get())` is well-formed, has well-defined behavior, and does not throw exceptions.

**4** *Effects:* Assigns `p` to the stored pointer, and then if and only if the old value of the stored pointer, `old_p`, was not equal to `nullptr`, calls `get_deleter()(old_p)`. [*Note 1*: The order of these operations is significant because the call to `get_deleter()` may destroy `*this`. — *end note*]

**5** *Postconditions:* `get() == p`. [*Note 2*: The postcondition does not hold if the call to `get_deleter()` destroys `*this` since `this->get()` is no longer a valid expression. — *end note*]

```cpp
void swap(unique_ptr& u) noexcept;
```

**6** *Preconditions:* `get_deleter()` is swappable ([[swappable.requirements]](swappable.requirements.md)) and does not throw an exception under `swap`.

**7** *Effects:* Invokes `swap` on the stored pointers and on the stored deleters of `*this` and `u`.
