# 20.11.3.4 Modifiers [util.smartptr.shared.mod]

[Contents](../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.3 Assignment \[util.smartptr.shared.assign\]](util.smartptr.shared.assign.md) · next: [20.11.3.5 Observers \[util.smartptr.shared.obs\]](util.smartptr.shared.obs.md)

```cpp
void swap(shared_ptr& r) noexcept;
```

**1** *Effects:* Exchanges the contents of `*this` and `r`.

```cpp
void reset() noexcept;
```

**2** *Effects:* Equivalent to `shared_ptr().swap(*this)`.

```cpp
template<class Y> void reset(Y* p);
```

**3** *Effects:* Equivalent to `shared_ptr(p).swap(*this)`.

```cpp
template<class Y, class D> void reset(Y* p, D d);
```

**4** *Effects:* Equivalent to `shared_ptr(p, d).swap(*this)`.

```cpp
template<class Y, class D, class A> void reset(Y* p, D d, A a);
```

**5** *Effects:* Equivalent to `shared_ptr(p, d, a).swap(*this)`.
