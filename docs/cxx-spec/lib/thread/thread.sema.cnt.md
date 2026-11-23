# 32.7.2 Class template `counting_semaphore` [thread.sema.cnt]

[Contents](../../README.md) · up: [32.7 Semaphore \[thread.sema\]](thread.sema.md) · prev: [32.7.1 Header `<semaphore>` synopsis \[semaphore.syn\]](semaphore.syn.md) · next: [32.8 Coordination types \[thread.coord\]](thread.coord.md)

```cpp
namespace std {
  template<ptrdiff_t least_max_value = implementation-defined>
  class counting_semaphore {
  public:
    static constexpr ptrdiff_t max() noexcept;

    constexpr explicit counting_semaphore(ptrdiff_t desired);
    ~counting_semaphore();

    counting_semaphore(const counting_semaphore&) = delete;
    counting_semaphore& operator=(const counting_semaphore&) = delete;

    void release(ptrdiff_t update = 1);
    void acquire();
    bool try_acquire() noexcept;
    template<class Rep, class Period>
      bool try_acquire_for(const chrono::duration<Rep, Period>& rel_time);
    template<class Clock, class Duration>
      bool try_acquire_until(const chrono::time_point<Clock, Duration>& abs_time);

  private:
    ptrdiff_t counter;          // exposition only
  };
}
```

**1** Class template `counting_semaphore` maintains an internal counter that is initialized when the semaphore is created. The counter is decremented when a thread acquires the semaphore, and is incremented when a thread releases the semaphore. If a thread tries to acquire the semaphore when the counter is zero, the thread will block until another thread increments the counter by releasing the semaphore.

**2** `least_max_value` shall be non-negative; otherwise the program is ill-formed.

**3** Concurrent invocations of the member functions of `counting_semaphore`, other than its destructor, do not introduce data races.

```cpp
static constexpr ptrdiff_t max() noexcept;
```

**4** *Returns:* The maximum value of `counter`. This value is greater than or equal to `least_max_value`.

```cpp
constexpr explicit counting_semaphore(ptrdiff_t desired);
```

**5** *Preconditions:* `desired >= 0` is `true`, and `desired <= max()` is `true`.

**6** *Effects:* Initializes `counter` with `desired`.

**7** *Throws:* Nothing.

```cpp
void release(ptrdiff_t update = 1);
```

**8** *Preconditions:* `update >= 0` is `true`, and `update <= max() - counter` is `true`.

**9** *Effects:* Atomically execute `counter += update`. Then, unblocks any threads that are waiting for `counter` to be greater than zero.

**10** *Synchronization:* Strongly happens before invocations of `try_acquire` that observe the result of the effects.

**11** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**12** *Error conditions:* Any of the error conditions allowed for mutex types ([[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md)).

```cpp
bool try_acquire() noexcept;
```

**13** *Effects:* Attempts to atomically decrement `counter` if it is positive, without blocking. If `counter` is not decremented, there is no effect and `try_acquire` immediately returns. An implementation may fail to decrement `counter` even if it is positive. [*Note 1*: This spurious failure is normally uncommon, but allows interesting implementations based on a simple compare and exchange ([[atomics]](../atomics/atomics.md)). — *end note*] An implementation should ensure that `try_acquire` does not consistently return `false` in the absence of contending semaphore operations.

**14** *Returns:* `true` if `counter` was decremented, otherwise `false`.

```cpp
void acquire();
```

**15** *Effects:* Repeatedly performs the following steps, in order:

- Evaluates `try_acquire`. If the result is `true`, returns.
- Blocks on `*this` until `counter` is greater than zero.

**16** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**17** *Error conditions:* Any of the error conditions allowed for mutex types ([[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md)).

```cpp
template<class Rep, class Period>
  bool try_acquire_for(const chrono::duration<Rep, Period>& rel_time);
template<class Clock, class Duration>
  bool try_acquire_until(const chrono::time_point<Clock, Duration>& abs_time);
```

**18** *Effects:* Repeatedly performs the following steps, in order:

- Evaluates `try_acquire()`. If the result is `true`, returns `true`.
- Blocks on `*this` until `counter` is greater than zero or until the timeout expires. If it is unblocked by the timeout expiring, returns `false`.

The timeout expires ([[thread.req.timing]](thread.req.timing.md)) when the current time is after `abs_time` (for `try_acquire_until`) or when at least `rel_time` has passed from the start of the function (for `try_acquire_for`).

**19** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)), or `system_error` when a non-timeout-related exception is required ([[thread.req.exception]](thread.req.exception.md)).

**20** *Error conditions:* Any of the error conditions allowed for mutex types ([[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md)).
