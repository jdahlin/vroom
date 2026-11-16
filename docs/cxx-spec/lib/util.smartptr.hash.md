# 20.11.7 Smart pointer hash support [util.smartptr.hash]

[Contents](../README.md) · up: [20.11 Smart pointers \[smartptr\]](smartptr.md) · prev: [20.11.6 Class template `enable_shared_from_this` \[util.smartptr.enab\]](util.smartptr.enab.md) · next: [20.12 Memory resources \[mem.res\]](mem.res.md)

```cpp
template<class T, class D> struct hash<unique_ptr<T, D>>;
```

**1** Letting `UP` be `unique_ptr<T,D>`, the specialization `hash<UP>` is enabled ([[unord.hash]](unord.hash.md)) if and only if `hash<typename UP::pointer>` is enabled. When enabled, for an object `p` of type `UP`, `hash<UP>()(p)` evaluates to the same value as `hash<typename UP::pointer>()(p.get())`. The member functions are not guaranteed to be `noexcept`.

```cpp
template<class T> struct hash<shared_ptr<T>>;
```

**2** For an object `p` of type `shared_ptr<T>`, `hash<shared_ptr<T>>()(p)` evaluates to the same value as `hash<typename shared_ptr<T>::element_type*>()(p.get())`.
