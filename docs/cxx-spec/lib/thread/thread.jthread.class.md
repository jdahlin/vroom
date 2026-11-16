# 32.4.3 Class `jthread` [thread.jthread.class]

[Contents](../../README.md) · up: [32.4 Threads \[thread.threads\]](thread.threads.md) · prev: [32.4.2.7 Specialized algorithms \[thread.thread.algorithm\]](thread.thread.algorithm.md) · next: [32.4.3.1 Constructors, move, and assignment \[thread.jthread.cons\]](thread.jthread.cons.md)

**1** The class `jthread` provides a mechanism to create a new thread of execution. The functionality is the same as for class `thread` ([[thread.thread.class]](thread.thread.class.md)) with the additional abilities to provide a `stop_token` ([[thread.stoptoken]](thread.stoptoken.md)) to the new thread of execution, make stop requests, and automatically join.

```cpp
namespace std {
  class jthread {
  public:
    // types
    using id = thread::id;
    using native_handle_type = thread::native_handle_type;

    // [thread.jthread.cons], constructors, move, and assignment
    jthread() noexcept;
    template<class F, class... Args> explicit jthread(F&& f, Args&&... args);
    ~jthread();
    jthread(const jthread&) = delete;
    jthread(jthread&&) noexcept;
    jthread& operator=(const jthread&) = delete;
    jthread& operator=(jthread&&) noexcept;

    // [thread.jthread.mem], members
    void swap(jthread&) noexcept;
    [[nodiscard]] bool joinable() const noexcept;
    void join();
    void detach();
    [[nodiscard]] id get_id() const noexcept;
    [[nodiscard]] native_handle_type native_handle();   // see [thread.req.native]

    // [thread.jthread.stop], stop token handling
    [[nodiscard]] stop_source get_stop_source() noexcept;
    [[nodiscard]] stop_token get_stop_token() const noexcept;
    bool request_stop() noexcept;

    // [thread.jthread.special], specialized algorithms
    friend void swap(jthread& lhs, jthread& rhs) noexcept;

    // [thread.jthread.static], static members
    [[nodiscard]] static unsigned int hardware_concurrency() noexcept;

  private:
    stop_source ssource;        // exposition only
  };
}
```

## Subclauses

- [32.4.3.1 Constructors, move, and assignment \[thread.jthread.cons\]](thread.jthread.cons.md)
- [32.4.3.2 Members \[thread.jthread.mem\]](thread.jthread.mem.md)
- [32.4.3.3 Stop token handling \[thread.jthread.stop\]](thread.jthread.stop.md)
- [32.4.3.4 Specialized algorithms \[thread.jthread.special\]](thread.jthread.special.md)
- [32.4.3.5 Static members \[thread.jthread.static\]](thread.jthread.static.md)
