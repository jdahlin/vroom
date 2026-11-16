# 20.11.1.1.3 `default_delete<T[]>` [unique.ptr.dltr.dflt1]

[Contents](../../README.md) · up: [20.11.1.1 Default deleters \[unique.ptr.dltr\]](unique.ptr.dltr.md) · prev: [20.11.1.1.2 `default_delete` \[unique.ptr.dltr.dflt\]](unique.ptr.dltr.dflt.md) · next: [20.11.1.2 `unique_ptr` for single objects \[unique.ptr.single\]](unique.ptr.single.md)

```cpp
namespace std {
  template<class T> struct default_delete<T[]> {
    constexpr default_delete() noexcept = default;
    template<class U> default_delete(const default_delete<U[]>&) noexcept;
    template<class U> void operator()(U* ptr) const;
  };
}
```

```cpp
template<class U> default_delete(const default_delete<U[]>& other) noexcept;
```

**1** *Constraints:* `U(*)[]` is convertible to `T(*)[]`.

**2** *Effects:* Constructs a `default_delete` object from another `default_delete<U[]>` object.

```cpp
template<class U> void operator()(U* ptr) const;
```

**3** *Mandates:* `U` is a complete type.

**4** *Constraints:* `U(*)[]` is convertible to `T(*)[]`.

**5** *Effects:* Calls `delete[]` on `ptr`.
