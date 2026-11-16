# 20.11.3.7 Comparison [util.smartptr.shared.cmp]

[Contents](../../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.6 Creation \[util.smartptr.shared.create\]](util.smartptr.shared.create.md) · next: [20.11.3.8 Specialized algorithms \[util.smartptr.shared.spec\]](util.smartptr.shared.spec.md)

```cpp
template<class T, class U>
  bool operator==(const shared_ptr<T>& a, const shared_ptr<U>& b) noexcept;
```

**1** *Returns:* `a.get() == b.get()`.

```cpp
template<class T>
  bool operator==(const shared_ptr<T>& a, nullptr_t) noexcept;
```

**2** *Returns:* `!a`.

```cpp
template<class T, class U>
  strong_ordering operator<=>(const shared_ptr<T>& a, const shared_ptr<U>& b) noexcept;
```

**3** *Returns:* `compare_three_way()(a.get(), b.get())`.

**4** [*Note 1*: Defining a comparison function allows `shared_ptr` objects to be used as keys in associative containers. — *end note*]

```cpp
template<class T>
  strong_ordering operator<=>(const shared_ptr<T>& a, nullptr_t) noexcept;
```

**5** *Returns:* `compare_three_way()(a.get(), nullptr)`.
