# 32.4.2 Class `thread` [thread.thread.class]

[Contents](../README.md) · up: [32.4 Threads \[thread.threads\]](thread.threads.md) · prev: [32.4.1 Header `<thread>` synopsis \[thread.syn\]](thread.syn.md) · next: [32.4.2.1 Class `thread::id` \[thread.thread.id\]](thread.thread.id.md)

**1** The class `thread` provides a mechanism to create a new thread of execution, to join with a thread (i.e., wait for a thread to complete), and to perform other operations that manage and query the state of a thread. A `thread` object uniquely represents a particular thread of execution. That representation may be transferred to other `thread` objects in such a way that no two `thread` objects simultaneously represent the same thread of execution. A thread of execution is *detached* when no `thread` object represents that thread. Objects of class `thread` can be in a state that does not represent a thread of execution. [*Note 1*: A `thread` object does not represent a thread of execution after default construction, after being moved from, or after a successful call to `detach` or `join`. — *end note*]

```cpp
namespace std {
  class thread {
  public:
    // types
    class id;
    using native_handle_type = implementation-defined;         // see [thread.req.native]

    // construct/copy/destroy
    thread() noexcept;
    template<class F, class... Args> explicit thread(F&& f, Args&&... args);
    ~thread();
    thread(const thread&) = delete;
    thread(thread&&) noexcept;
    thread& operator=(const thread&) = delete;
    thread& operator=(thread&&) noexcept;

    // members
    void swap(thread&) noexcept;
    bool joinable() const noexcept;
    void join();
    void detach();
    id get_id() const noexcept;
    native_handle_type native_handle();                         // see [thread.req.native]

    // static members
    static unsigned int hardware_concurrency() noexcept;
  };
}
```

## Subclauses

- [32.4.2.1 Class `thread::id` \[thread.thread.id\]](thread.thread.id.md)
- [32.4.2.2 Constructors \[thread.thread.constr\]](thread.thread.constr.md)
- [32.4.2.3 Destructor \[thread.thread.destr\]](thread.thread.destr.md)
- [32.4.2.4 Assignment \[thread.thread.assign\]](thread.thread.assign.md)
- [32.4.2.5 Members \[thread.thread.member\]](thread.thread.member.md)
- [32.4.2.6 Static members \[thread.thread.static\]](thread.thread.static.md)
- [32.4.2.7 Specialized algorithms \[thread.thread.algorithm\]](thread.thread.algorithm.md)
