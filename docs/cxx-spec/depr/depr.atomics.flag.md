# D.24.4 Flag type and operations [depr.atomics.flag]

[Contents](../README.md) · up: [D.24 Deprecated atomic operations \[depr.atomics\]](depr.atomics.md) · prev: [D.24.3 Operations on atomic types \[depr.atomics.types.operations\]](depr.atomics.types.operations.md)

```cpp
#define ATOMIC_FLAG_INIT see below
```

**1** *Remarks:* The macro `ATOMIC_FLAG_INIT` is defined in such a way that it can be used to initialize an object of type `atomic_flag` to the clear state. The macro can be used in the form:

```cpp
atomic_flag guard = ATOMIC_FLAG_INIT;
```

It is unspecified whether the macro can be used in other initialization contexts. For a complete static-duration object, that initialization shall be static.
