# 32.8.1.2 Class `latch` [thread.latch.class]

[Contents](../README.md) · up: [32.8.1 Latches \[thread.latch\]](thread.latch.md) · prev: [32.8.1.1 Header `<latch>` synopsis \[latch.syn\]](latch.syn.md) · next: [32.8.2 Barriers \[thread.barrier\]](thread.barrier.md)

```cpp
namespace std {
  class latch {
  public:
    static constexpr ptrdiff_t max() noexcept;

    constexpr explicit latch(ptrdiff_t expected);
    ~latch();

    latch(const latch&) = delete;
    latch& operator=(const latch&) = delete;

    void count_down(ptrdiff_t update = 1);
    bool try_wait() const noexcept;
    void wait() const;
    void arrive_and_wait(ptrdiff_t update = 1);

  private:
    ptrdiff_t counter;  // exposition only
  };
}
```

**1** A `latch` maintains an internal counter that is initialized when the latch is created. Threads can block on the latch object, waiting for counter to be decremented to zero.

**2** Concurrent invocations of the member functions of `latch`, other than its destructor, do not introduce data races.

```cpp
static constexpr ptrdiff_t max() noexcept;
```

**3** *Returns:* The maximum value of `counter` that the implementation supports.

```cpp
constexpr explicit latch(ptrdiff_t expected);
```

**4** *Preconditions:* `expected >= 0` is `true` and `expected <= max()` is `true`.

**5** *Effects:* Initializes `counter` with `expected`.

**6** *Throws:* Nothing.

```cpp
void count_down(ptrdiff_t update = 1);
```

**7** *Preconditions:* `update >= 0` is `true`, and `update <= counter` is `true`.

**8** *Effects:* Atomically decrements `counter` by `update`. If `counter` is equal to zero, unblocks all threads blocked on `*this`.

**9** *Synchronization:* Strongly happens before the returns from all calls that are unblocked.

**10** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**11** *Error conditions:* Any of the error conditions allowed for mutex types ([[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md)).

```cpp
bool try_wait() const noexcept;
```

**12** *Returns:* With very low probability `false`. Otherwise `counter == 0`.

```cpp
void wait() const;
```

**13** *Effects:* If `counter` equals zero, returns immediately. Otherwise, blocks on `*this` until a call to `count_down` that decrements `counter` to zero.

**14** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**15** *Error conditions:* Any of the error conditions allowed for mutex types ([[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md)).

```cpp
void arrive_and_wait(ptrdiff_t update = 1);
```

**16** *Effects:* Equivalent to:

```cpp
count_down(update);
wait();
```
