# 20.11.3.9 Casts [util.smartptr.shared.cast]

[Contents](../../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.8 Specialized algorithms \[util.smartptr.shared.spec\]](util.smartptr.shared.spec.md) · next: [20.11.3.10 `get_deleter` \[util.smartptr.getdeleter\]](util.smartptr.getdeleter.md)

```cpp
template<class T, class U>
  shared_ptr<T> static_pointer_cast(const shared_ptr<U>& r) noexcept;
template<class T, class U>
  shared_ptr<T> static_pointer_cast(shared_ptr<U>&& r) noexcept;
```

**1** *Mandates:* The expression `static_cast<T*>((U*)nullptr)` is well-formed.

**2** *Returns:*

```cpp
shared_ptr<T>(R, static_cast<typename shared_ptr<T>::element_type*>(r.get()))
```

where `R` is `r` for the first overload, and `std::move(r)` for the second.

**3** [*Note 1*: The seemingly equivalent expression `shared_ptr<T>(static_cast<T*>(r.get()))` will eventually result in undefined behavior, attempting to delete the same object twice. — *end note*]

```cpp
template<class T, class U>
  shared_ptr<T> dynamic_pointer_cast(const shared_ptr<U>& r) noexcept;
template<class T, class U>
  shared_ptr<T> dynamic_pointer_cast(shared_ptr<U>&& r) noexcept;
```

**4** *Mandates:* The expression `dynamic_cast<T*>((U*)nullptr)` is well-formed. The expression `dynamic_cast<typename shared_ptr<T>::element_type*>(r.get())` is well formed.

**5** *Preconditions:* The expression `dynamic_cast<typename shared_ptr<T>::element_type*>(r.get())` has well-defined behavior.

**6** *Returns:*

- When `dynamic_cast<typename shared_ptr<T>::element_type*>(r.get())` returns a non-null value `p`, `shared_ptr<T>(R, p)`, where `R` is `r` for the first overload, and `std::move(r)` for the second.
- Otherwise, `shared_ptr<T>()`.

**7** [*Note 2*: The seemingly equivalent expression `shared_ptr<T>(dynamic_cast<T*>(r.get()))` will eventually result in undefined behavior, attempting to delete the same object twice. — *end note*]

```cpp
template<class T, class U>
  shared_ptr<T> const_pointer_cast(const shared_ptr<U>& r) noexcept;
template<class T, class U>
  shared_ptr<T> const_pointer_cast(shared_ptr<U>&& r) noexcept;
```

**8** *Mandates:* The expression `const_cast<T*>((U*)nullptr)` is well-formed.

**9** *Returns:*

```cpp
shared_ptr<T>(R, const_cast<typename shared_ptr<T>::element_type*>(r.get()))
```

where `R` is `r` for the first overload, and `std::move(r)` for the second.

**10** [*Note 3*: The seemingly equivalent expression `shared_ptr<T>(const_cast<T*>(r.get()))` will eventually result in undefined behavior, attempting to delete the same object twice. — *end note*]

```cpp
template<class T, class U>
  shared_ptr<T> reinterpret_pointer_cast(const shared_ptr<U>& r) noexcept;
template<class T, class U>
  shared_ptr<T> reinterpret_pointer_cast(shared_ptr<U>&& r) noexcept;
```

**11** *Mandates:* The expression `reinterpret_cast<T*>((U*)nullptr)` is well-formed.

**12** *Returns:*

```cpp
shared_ptr<T>(R, reinterpret_cast<typename shared_ptr<T>::element_type*>(r.get()))
```

where `R` is `r` for the first overload, and `std::move(r)` for the second.

**13** [*Note 4*: The seemingly equivalent expression `shared_ptr<T>(reinterpret_cast<T*>(r.get()))` will eventually result in undefined behavior, attempting to delete the same object twice. — *end note*]
