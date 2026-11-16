# 23.5.7.1 Class `unreachable_sentinel_t` [unreachable.sentinel]

[Contents](../README.md) · up: [23.5.7 Unreachable sentinel \[unreachable.sentinels\]](unreachable.sentinels.md) · prev: [23.5.7 Unreachable sentinel \[unreachable.sentinels\]](unreachable.sentinels.md) · next: [23.6 Stream iterators \[stream.iterators\]](stream.iterators.md)

**1** Class `unreachable_sentinel_t` can be used with any `weakly_incrementable` type to denote the “upper bound” of an unbounded interval.

**2** [*Example 1*:

```cpp
char* p;
// set p to point to a character buffer containing newlines
char* nl = find(p, unreachable_sentinel, '\n');
```

Provided a newline character really exists in the buffer, the use of `unreachable_sentinel` above potentially makes the call to `find` more efficient since the loop test against the sentinel does not require a conditional branch. — *end example*]

```cpp
namespace std {
  struct unreachable_sentinel_t {
    template<weakly_incrementable I>
      friend constexpr bool operator==(unreachable_sentinel_t, const I&) noexcept
      { return false; }
  };
}
```
