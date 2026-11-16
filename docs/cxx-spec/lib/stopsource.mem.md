# 32.3.4.2 Members [stopsource.mem]

[Contents](../README.md) · up: [32.3.4 Class `stop_source` \[stopsource\]](stopsource.md) · prev: [32.3.4.1 Constructors, copy, and assignment \[stopsource.cons\]](stopsource.cons.md) · next: [32.3.4.3 Non-member functions \[stopsource.nonmembers\]](stopsource.nonmembers.md)

```cpp
[[nodiscard]] stop_token get_token() const noexcept;
```

**1** *Returns:* `stop_token()` if `stop_possible()` is `false`; otherwise a new associated `stop_token` object.

```cpp
[[nodiscard]] bool stop_possible() const noexcept;
```

**2** *Returns:* `true` if `*this` has ownership of a stop state; otherwise, `false`.

```cpp
[[nodiscard]] bool stop_requested() const noexcept;
```

**3** *Returns:* `true` if `*this` has ownership of a stop state that has received a stop request; otherwise, `false`.

```cpp
bool request_stop() noexcept;
```

**4** *Effects:* If `*this` does not have ownership of a stop state, returns `false`. Otherwise, atomically determines whether the owned stop state has received a stop request, and if not, makes a stop request. The determination and making of the stop request are an atomic read-modify-write operation ([[intro.races]](../intro.races.md)). If the request was made, the callbacks registered by associated `stop_callback` objects are synchronously called. If an invocation of a callback exits via an exception then `terminate` is called ([[except.terminate]](../except.terminate.md)). [*Note 1*: A stop request includes notifying all condition variables of type `condition_variable_any` temporarily registered during an interruptible wait ([[thread.condvarany.intwait]](thread.condvarany.intwait.md)). — *end note*]

**5** *Postconditions:* `stop_possible()` is `false` or `stop_requested()` is `true`.

**6** *Returns:* `true` if this call made a stop request; otherwise `false`.
