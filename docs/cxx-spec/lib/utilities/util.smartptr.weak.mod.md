# 20.11.4.4 Modifiers [util.smartptr.weak.mod]

[Contents](../../README.md) · up: [20.11.4 Class template `weak_ptr` \[util.smartptr.weak\]](util.smartptr.weak.md) · prev: [20.11.4.3 Assignment \[util.smartptr.weak.assign\]](util.smartptr.weak.assign.md) · next: [20.11.4.5 Observers \[util.smartptr.weak.obs\]](util.smartptr.weak.obs.md)

```cpp
void swap(weak_ptr& r) noexcept;
```

**1** *Effects:* Exchanges the contents of `*this` and `r`.

```cpp
void reset() noexcept;
```

**2** *Effects:* Equivalent to `weak_ptr().swap(*this)`.
