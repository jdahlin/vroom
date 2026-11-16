# 17.13.4 Signal handlers [support.signal]

[Contents](../README.md) · up: [17.13 Other runtime support \[support.runtime\]](support.runtime.md) · prev: [17.13.3 Header `<csignal>` synopsis \[csignal.syn\]](csignal.syn.md) · next: [18 Concepts library \[concepts\]](concepts.md)

**1** A call to the function `signal` synchronizes with any resulting invocation of the signal handler so installed.

**2** A *plain lock-free atomic operation* is an invocation of a function `f` from [[atomics]](atomics.md), such that:

- `f` is the function `atomic_is_lock_free()`, or
- `f` is the member function `is_lock_free()`, or
- `f` is a non-static member function invoked on an object `A`, such that `A.is_lock_free()` yields `true`, or
- `f` is a non-member function, and for every pointer-to-atomic argument `A` passed to `f`, `atomic_is_lock_free(A)` yields `true`.

**3** An evaluation is *signal-safe* unless it includes one of the following:

- a call to any standard library function, except for plain lock-free atomic operations and functions explicitly identified as signal-safe; [*Note 1*: This implicitly excludes the use of `new` and `delete` expressions that rely on a library-provided memory allocator. — *end note*]
- an access to an object with thread storage duration;
- a `dynamic_cast` expression;
- throwing of an exception;
- control entering a *try-block* or *function-try-block*;
- initialization of a variable with static storage duration requiring dynamic initialization ([[basic.start.dynamic]](../basic.start.dynamic.md), [[stmt.dcl]](../stmt.dcl.md))[^1]; or
- waiting for the completion of the initialization of a variable with static storage duration ([[stmt.dcl]](../stmt.dcl.md)).

A signal handler invocation has undefined behavior if it includes an evaluation that is not signal-safe.

**4** The function `signal` is signal-safe if it is invoked with the first argument equal to the signal number corresponding to the signal that caused the invocation of the handler.

*See also:* ISO C 7.14

[^1]: Such initialization might occur because it is the first odr-use ([[basic.def.odr]](../basic.def.odr.md)) of that variable.
