# 20.11.1.3.2 Assignment [unique.ptr.runtime.asgn]

[Contents](../README.md) · up: [20.11.1.3 `unique_ptr` for array objects with a runtime length \[unique.ptr.runtime\]](unique.ptr.runtime.md) · prev: [20.11.1.3.1 Constructors \[unique.ptr.runtime.ctor\]](unique.ptr.runtime.ctor.md) · next: [20.11.1.3.3 Observers \[unique.ptr.runtime.observers\]](unique.ptr.runtime.observers.md)

```cpp
template<class U, class E> unique_ptr& operator=(unique_ptr<U, E>&& u)noexcept;
```

**1** This operator behaves the same as in the primary template.

**2** *Constraints:* Where `UP` is `unique_ptr<U, E>`:

- `U` is an array type, and
- `pointer` is the same type as `element_type*`, and
- `UP::pointer` is the same type as `UP::element_type*`, and
- `UP::element_type(*)[]` is convertible to `element_type(*)[]`, and
- `is_assignable_v<D&, E&&>` is `true`.

[*Note 1*: This replaces the *Constraints:* specification of the primary template. — *end note*]
