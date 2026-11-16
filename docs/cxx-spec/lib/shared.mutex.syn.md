# 32.5.2 Header `<shared_mutex>` synopsis [shared.mutex.syn]

[Contents](../README.md) · up: [32.5 Mutual exclusion \[thread.mutex\]](thread.mutex.md) · prev: [32.5.1 Header `<mutex>` synopsis \[mutex.syn\]](mutex.syn.md) · next: [32.5.3 Mutex requirements \[thread.mutex.requirements\]](thread.mutex.requirements.md)

```cpp
namespace std {
  class shared_mutex;
  class shared_timed_mutex;
  template<class Mutex> class shared_lock;
  template<class Mutex>
    void swap(shared_lock<Mutex>& x, shared_lock<Mutex>& y) noexcept;
}
```
