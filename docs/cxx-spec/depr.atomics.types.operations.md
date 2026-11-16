# D.24.3 Operations on atomic types [depr.atomics.types.operations]

[Contents](README.md) · up: [D.24 Deprecated atomic operations \[depr.atomics\]](depr.atomics.md) · prev: [D.24.2 Non-member functions \[depr.atomics.nonmembers\]](depr.atomics.nonmembers.md) · next: [D.24.4 Flag type and operations \[depr.atomics.flag\]](depr.atomics.flag.md)

```cpp
#define ATOMIC_VAR_INIT(value) see below
```

**1** The macro expands to a token sequence suitable for constant initialization of an atomic variable of static storage duration of a type that is initialization-compatible with `value`. [*Note 1*: This operation may need to initialize locks. — *end note*] Concurrent access to the variable being initialized, even via an atomic operation, constitutes a data race. [*Example 1*:

```cpp
atomic<int> v = ATOMIC_VAR_INIT(5);
```

— *end example*]
