# 32.5.6.1 Struct `once_flag` [thread.once.onceflag]

[Contents](../../README.md) · up: [32.5.6 Call once \[thread.once\]](thread.once.md) · prev: [32.5.6 Call once \[thread.once\]](thread.once.md) · next: [32.5.6.2 Function `call_once` \[thread.once.callonce\]](thread.once.callonce.md)

```cpp
namespace std {
  struct once_flag {
    constexpr once_flag() noexcept;

    once_flag(const once_flag&) = delete;
    once_flag& operator=(const once_flag&) = delete;
  };
}
```

**1** The class `once_flag` is an opaque data structure that `call_once` uses to initialize data without causing a data race or deadlock.

```cpp
constexpr once_flag() noexcept;
```

**2** *Synchronization:* The construction of a `once_flag` object is not synchronized.

**3** *Postconditions:* The object’s internal state is set to indicate to an invocation of `call_once` with the object as its initial argument that no function has been called.
