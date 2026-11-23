# D.24.2 Non-member functions [depr.atomics.nonmembers]

[Contents](../README.md) · up: [D.24 Deprecated atomic operations \[depr.atomics\]](depr.atomics.md) · prev: [D.24.1 Volatile access \[depr.atomics.volatile\]](depr.atomics.volatile.md) · next: [D.24.3 Operations on atomic types \[depr.atomics.types.operations\]](depr.atomics.types.operations.md)

```cpp
template<class T>
  void atomic_init(volatile atomic<T>* object, typename atomic<T>::value_type desired) noexcept;
template<class T>
  void atomic_init(atomic<T>* object, typename atomic<T>::value_type desired) noexcept;
```

**1** *Effects:* Equivalent to: `atomic_store_explicit(object, desired, memory_order::relaxed);`
