# 20.11.1.2.2 Destructor [unique.ptr.single.dtor]

[Contents](../../README.md) · up: [20.11.1.2 `unique_ptr` for single objects \[unique.ptr.single\]](unique.ptr.single.md) · prev: [20.11.1.2.1 Constructors \[unique.ptr.single.ctor\]](unique.ptr.single.ctor.md) · next: [20.11.1.2.3 Assignment \[unique.ptr.single.asgn\]](unique.ptr.single.asgn.md)

```cpp
~unique_ptr();
```

**1** *Preconditions:* The expression `get_deleter()(get())` is well-formed, has well-defined behavior, and does not throw exceptions. [*Note 1*: The use of `default_delete` requires `T` to be a complete type. — *end note*]

**2** *Effects:* If `get() == nullptr` there are no effects. Otherwise `get_deleter()(get())`.
