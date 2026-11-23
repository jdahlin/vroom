# 20.11.4.1 Constructors [util.smartptr.weak.const]

[Contents](../../README.md) · up: [20.11.4 Class template `weak_ptr` \[util.smartptr.weak\]](util.smartptr.weak.md) · prev: [20.11.4 Class template `weak_ptr` \[util.smartptr.weak\]](util.smartptr.weak.md) · next: [20.11.4.2 Destructor \[util.smartptr.weak.dest\]](util.smartptr.weak.dest.md)

```cpp
constexpr weak_ptr() noexcept;
```

**1** *Effects:* Constructs an empty `weak_ptr` object.

**2** *Postconditions:* `use_count() == 0`.

```cpp
weak_ptr(const weak_ptr& r) noexcept;
template<class Y> weak_ptr(const weak_ptr<Y>& r) noexcept;
template<class Y> weak_ptr(const shared_ptr<Y>& r) noexcept;
```

**3** *Constraints:* For the second and third constructors, `Y*` is compatible with `T*`.

**4** *Effects:* If `r` is empty, constructs an empty `weak_ptr` object; otherwise, constructs a `weak_ptr` object that shares ownership with `r` and stores a copy of the pointer stored in `r`.

**5** *Postconditions:* `use_count() == r.use_count()`.

```cpp
weak_ptr(weak_ptr&& r) noexcept;
template<class Y> weak_ptr(weak_ptr<Y>&& r) noexcept;
```

**6** *Constraints:* For the second constructor, `Y*` is compatible with `T*`.

**7** *Effects:* Move constructs a `weak_ptr` instance from `r`.

**8** *Postconditions:* `*this` shall contain the old value of `r`. `r` shall be empty. `r.use_count() == 0`.
