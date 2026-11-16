# 32.4.4 Namespace `this_thread` [thread.thread.this]

[Contents](../../README.md) · up: [32.4 Threads \[thread.threads\]](thread.threads.md) · prev: [32.4.3.5 Static members \[thread.jthread.static\]](thread.jthread.static.md) · next: [32.5 Mutual exclusion \[thread.mutex\]](thread.mutex.md)

```cpp
namespace std::this_thread {
  thread::id get_id() noexcept;

  void yield() noexcept;
  template<class Clock, class Duration>
    void sleep_until(const chrono::time_point<Clock, Duration>& abs_time);
  template<class Rep, class Period>
    void sleep_for(const chrono::duration<Rep, Period>& rel_time);
}
```

```cpp
thread::id this_thread::get_id() noexcept;
```

**1** *Returns:* An object of type `thread::id` that uniquely identifies the current thread of execution. No other thread of execution has this id and this thread of execution always has this id. The object returned does not compare equal to a default constructed `thread::id`.

```cpp
void this_thread::yield() noexcept;
```

**2** *Effects:* Offers the implementation the opportunity to reschedule.

**3** *Synchronization:* None.

```cpp
template<class Clock, class Duration>
  void sleep_until(const chrono::time_point<Clock, Duration>& abs_time);
```

**4** *Effects:* Blocks the calling thread for the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time`.

**5** *Synchronization:* None.

**6** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

```cpp
template<class Rep, class Period>
  void sleep_for(const chrono::duration<Rep, Period>& rel_time);
```

**7** *Effects:* Blocks the calling thread for the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time`.

**8** *Synchronization:* None.

**9** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).
