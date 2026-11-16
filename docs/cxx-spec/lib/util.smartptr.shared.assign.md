# 20.11.3.3 Assignment [util.smartptr.shared.assign]

[Contents](../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.2 Destructor \[util.smartptr.shared.dest\]](util.smartptr.shared.dest.md) · next: [20.11.3.4 Modifiers \[util.smartptr.shared.mod\]](util.smartptr.shared.mod.md)

```cpp
shared_ptr& operator=(const shared_ptr& r) noexcept;
template<class Y> shared_ptr& operator=(const shared_ptr<Y>& r) noexcept;
```

**1** *Effects:* Equivalent to `shared_ptr(r).swap(*this)`.

**2** *Returns:* `*this`.

**3** [*Note 1*: The use count updates caused by the temporary object construction and destruction are not observable side effects, so the implementation may meet the effects (and the implied guarantees) via different means, without creating a temporary. In particular, in the example:

```cpp
shared_ptr<int> p(new int);
shared_ptr<void> q(p);
p = p;
q = p;
```

both assignments may be no-ops. — *end note*]

```cpp
shared_ptr& operator=(shared_ptr&& r) noexcept;
template<class Y> shared_ptr& operator=(shared_ptr<Y>&& r) noexcept;
```

**4** *Effects:* Equivalent to `shared_ptr(std::move(r)).swap(*this)`.

**5** *Returns:* `*this`.

```cpp
template<class Y, class D> shared_ptr& operator=(unique_ptr<Y, D>&& r);
```

**6** *Effects:* Equivalent to `shared_ptr(std::move(r)).swap(*this)`.

**7** *Returns:* `*this`.
