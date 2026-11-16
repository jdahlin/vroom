# 31.3 Type aliases [atomics.alias]

[Contents](../../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31.2 Header `<atomic>` synopsis \[atomics.syn\]](atomics.syn.md) · next: [31.4 Order and consistency \[atomics.order\]](atomics.order.md)

**1** The type aliases `atomic_intN_t`, `atomic_uintN_t`, `atomic_intptr_t`, and `atomic_uintptr_t` are defined if and only if `intN_t`, `uintN_t`, `intptr_t`, and `uintptr_t` are defined, respectively.

**2** The type aliases `atomic_signed_lock_free` and `atomic_unsigned_lock_free` name specializations of `atomic` whose template arguments are integral types, respectively signed and unsigned, and whose `is_always_lock_free` property is `true`. [*Note 1*: These aliases are optional in freestanding implementations ([[compliance]](../library/compliance.md)). — *end note*] Implementations should choose for these aliases the integral specializations of `atomic` for which the atomic waiting and notifying operations ([[atomics.wait]](atomics.wait.md)) are most efficient.
