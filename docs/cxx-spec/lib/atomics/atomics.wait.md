# 31.6 Waiting and notifying [atomics.wait]

[Contents](../../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31.5 Lock-free property \[atomics.lockfree\]](atomics.lockfree.md) · next: [31.7 Class template `atomic_ref` \[atomics.ref.generic\]](atomics.ref.generic.md)

**1** *Atomic waiting operations* and *atomic notifying operations* provide a mechanism to wait for the value of an atomic object to change more efficiently than can be achieved with polling. An atomic waiting operation may block until it is unblocked by an atomic notifying operation, according to each function’s effects. [*Note 1*: Programs are not guaranteed to observe transient atomic values, an issue known as the A-B-A problem, resulting in continued blocking if a condition is only temporarily met. — *end note*]

**2** [*Note 2*: The following functions are atomic waiting operations:

- `atomic<T>::wait`,
- `atomic_flag::wait`,
- `atomic_wait` and `atomic_wait_explicit`,
- `atomic_flag_wait` and `atomic_flag_wait_explicit`, and
- `atomic_ref<T>::wait`.

— *end note*]

**3** [*Note 3*: The following functions are atomic notifying operations:

- `atomic<T>::notify_one` and `atomic<T>::notify_all`,
- `atomic_flag::notify_one` and `atomic_flag::notify_all`,
- `atomic_notify_one` and `atomic_notify_all`,
- `atomic_flag_notify_one` and `atomic_flag_notify_all`, and
- `atomic_ref<T>::notify_one` and `atomic_ref<T>::notify_all`.

— *end note*]

**4** A call to an atomic waiting operation on an atomic object `M` is *eligible to be unblocked* by a call to an atomic notifying operation on `M` if there exist side effects `X` and `Y` on `M` such that:

- the atomic waiting operation has blocked after observing the result of `X`,
- `X` precedes `Y` in the modification order of `M`, and
- `Y` happens before the call to the atomic notifying operation.
