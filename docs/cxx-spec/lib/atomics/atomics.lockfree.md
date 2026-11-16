# 31.5 Lock-free property [atomics.lockfree]

[Contents](../../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31.4 Order and consistency \[atomics.order\]](atomics.order.md) · next: [31.6 Waiting and notifying \[atomics.wait\]](atomics.wait.md)

```cpp
#define ATOMIC_BOOL_LOCK_FREE unspecified
#define ATOMIC_CHAR_LOCK_FREE unspecified
#define ATOMIC_CHAR8_T_LOCK_FREE unspecified
#define ATOMIC_CHAR16_T_LOCK_FREE unspecified
#define ATOMIC_CHAR32_T_LOCK_FREE unspecified
#define ATOMIC_WCHAR_T_LOCK_FREE unspecified
#define ATOMIC_SHORT_LOCK_FREE unspecified
#define ATOMIC_INT_LOCK_FREE unspecified
#define ATOMIC_LONG_LOCK_FREE unspecified
#define ATOMIC_LLONG_LOCK_FREE unspecified
#define ATOMIC_POINTER_LOCK_FREE unspecified
```

**1** The `ATOMIC_..._LOCK_FREE` macros indicate the lock-free property of the corresponding atomic types, with the signed and unsigned variants grouped together. The properties also apply to the corresponding (partial) specializations of the `atomic` template. A value of 0 indicates that the types are never lock-free. A value of 1 indicates that the types are sometimes lock-free. A value of 2 indicates that the types are always lock-free.

**2** At least one signed integral specialization of the `atomic` template, along with the specialization for the corresponding unsigned type ([[basic.fundamental]](../../basic/basic.fundamental.md)), is always lock-free. [*Note 1*: This requirement is optional in freestanding implementations ([[compliance]](../library/compliance.md)). — *end note*]

**3** The function `atomic_is_lock_free` ([[atomics.types.operations]](atomics.types.operations.md)) indicates whether the object is lock-free. In any given program execution, the result of the lock-free query shall be consistent for all pointers of the same type.

**4** Atomic operations that are not lock-free are considered to potentially block ([[intro.progress]](../../basic/intro.progress.md)).

**5** [*Note 2*: Operations that are lock-free should also be address-free. That is, atomic operations on the same memory location via two different addresses will communicate atomically. The implementation should not depend on any per-process state. This restriction enables communication by memory that is mapped into a process more than once and by memory that is shared between two processes. — *end note*]
