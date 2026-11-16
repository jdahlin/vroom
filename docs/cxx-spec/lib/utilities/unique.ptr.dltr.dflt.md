# 20.11.1.1.2 `default_delete` [unique.ptr.dltr.dflt]

[Contents](../../README.md) · up: [20.11.1.1 Default deleters \[unique.ptr.dltr\]](unique.ptr.dltr.md) · prev: [20.11.1.1.1 In general \[unique.ptr.dltr.general\]](unique.ptr.dltr.general.md) · next: [20.11.1.1.3 `default_delete<T[]>` \[unique.ptr.dltr.dflt1\]](unique.ptr.dltr.dflt1.md)

```cpp
namespace std {
  template<class T> struct default_delete {
    constexpr default_delete() noexcept = default;
    template<class U> default_delete(const default_delete<U>&) noexcept;
    void operator()(T*) const;
  };
}
```

```cpp
template<class U> default_delete(const default_delete<U>& other) noexcept;
```

**1** *Constraints:* `U*` is implicitly convertible to `T*`.

**2** *Effects:* Constructs a `default_delete` object from another `default_delete<U>` object.

```cpp
void operator()(T* ptr) const;
```

**3** *Mandates:* `T` is a complete type.

**4** *Effects:* Calls `delete` on `ptr`.
