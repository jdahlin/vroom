# 20.11.1.3.1 Constructors [unique.ptr.runtime.ctor]

[Contents](../README.md) · up: [20.11.1.3 `unique_ptr` for array objects with a runtime length \[unique.ptr.runtime\]](unique.ptr.runtime.md) · prev: [20.11.1.3 `unique_ptr` for array objects with a runtime length \[unique.ptr.runtime\]](unique.ptr.runtime.md) · next: [20.11.1.3.2 Assignment \[unique.ptr.runtime.asgn\]](unique.ptr.runtime.asgn.md)

```cpp
template<class U> explicit unique_ptr(U p) noexcept;
```

**1** This constructor behaves the same as the constructor in the primary template that takes a single parameter of type `pointer`.

**2** *Constraints:*

- `U` is the same type as `pointer`, or
- `pointer` is the same type as `element_type*`, `U` is a pointer type `V*`, and `V(*)[]` is convertible to `element_type(*)[]`.

```cpp
template<class U> unique_ptr(U p, see below d) noexcept;
template<class U> unique_ptr(U p, see below d) noexcept;
```

**3** These constructors behave the same as the constructors in the primary template that take a parameter of type `pointer` and a second parameter.

**4** *Constraints:*

- `U` is the same type as `pointer`,
- `U` is `nullptr_t`, or
- `pointer` is the same type as `element_type*`, `U` is a pointer type `V*`, and `V(*)[]` is convertible to `element_type(*)[]`.

```cpp
template<class U, class E> unique_ptr(unique_ptr<U, E>&& u) noexcept;
```

**5** This constructor behaves the same as in the primary template.

**6** *Constraints:* Where `UP` is `unique_ptr<U, E>`:

- `U` is an array type, and
- `pointer` is the same type as `element_type*`, and
- `UP::pointer` is the same type as `UP::element_type*`, and
- `UP::element_type(*)[]` is convertible to `element_type(*)[]`, and
- either `D` is a reference type and `E` is the same type as `D`, or `D` is not a reference type and `E` is implicitly convertible to `D`.

[*Note 1*: This replaces the *Constraints:* specification of the primary template. — *end note*]
