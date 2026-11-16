# 32.4.1 Header `<thread>` synopsis [thread.syn]

[Contents](../README.md) · up: [32.4 Threads \[thread.threads\]](thread.threads.md) · prev: [32.4 Threads \[thread.threads\]](thread.threads.md) · next: [32.4.2 Class `thread` \[thread.thread.class\]](thread.thread.class.md)

```cpp
#include <compare>              // see [compare.syn]
#include <initializer_list>     // see [initializer.list.syn]

namespace std {
  class thread;

  void swap(thread& x, thread& y) noexcept;

  // [thread.jthread.class] class jthread
  class jthread;

  namespace this_thread {
    thread::id get_id() noexcept;

    void yield() noexcept;
    template<class Clock, class Duration>
      void sleep_until(const chrono::time_point<Clock, Duration>& abs_time);
    template<class Rep, class Period>
      void sleep_for(const chrono::duration<Rep, Period>& rel_time);
  }
}
```
