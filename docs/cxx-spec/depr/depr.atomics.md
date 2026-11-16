# D.24 Deprecated atomic operations [depr.atomics]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.23 Deprecated filesystem path factory functions \[depr.fs.path.factory\]](depr.fs.path.factory.md) · next: [D.24.1 Volatile access \[depr.atomics.volatile\]](depr.atomics.volatile.md)

**1** The header `<atomics>` ([[atomics.syn]](../lib/atomics/atomics.syn.md)) has the following additions.

```cpp
namespace std {
  template<class T>
    void atomic_init(volatile atomic<T>*, typename atomic<T>::value_type) noexcept;
  template<class T>
    void atomic_init(atomic<T>*, typename atomic<T>::value_type) noexcept;

  #define ATOMIC_VAR_INIT(value) see below

  #define ATOMIC_FLAG_INIT see below
}
```

## Subclauses

- [D.24.1 Volatile access \[depr.atomics.volatile\]](depr.atomics.volatile.md)
- [D.24.2 Non-member functions \[depr.atomics.nonmembers\]](depr.atomics.nonmembers.md)
- [D.24.3 Operations on atomic types \[depr.atomics.types.operations\]](depr.atomics.types.operations.md)
- [D.24.4 Flag type and operations \[depr.atomics.flag\]](depr.atomics.flag.md)
