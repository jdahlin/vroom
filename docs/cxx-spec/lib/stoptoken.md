# 32.3.3 Class `stop_token` [stoptoken]

[Contents](../README.md) · up: [32.3 Stop tokens \[thread.stoptoken\]](thread.stoptoken.md) · prev: [32.3.2 Header `<stop_token>` synopsis \[thread.stoptoken.syn\]](thread.stoptoken.syn.md) · next: [32.3.3.1 Constructors, copy, and assignment \[stoptoken.cons\]](stoptoken.cons.md)

**1** The class `stop_token` provides an interface for querying whether a stop request has been made (`stop_requested`) or can ever be made (`stop_possible`) using an associated `stop_source` object ([[stopsource]](stopsource.md)). A `stop_token` can also be passed to a `stop_callback` ([[stopcallback]](stopcallback.md)) constructor to register a callback to be called when a stop request has been made from an associated `stop_source`.

```cpp
namespace std {
  class stop_token {
  public:
    // [stoptoken.cons], constructors, copy, and assignment
    stop_token() noexcept;

    stop_token(const stop_token&) noexcept;
    stop_token(stop_token&&) noexcept;
    stop_token& operator=(const stop_token&) noexcept;
    stop_token& operator=(stop_token&&) noexcept;
    ~stop_token();
    void swap(stop_token&) noexcept;

    // [stoptoken.mem], stop handling
    [[nodiscard]] bool stop_requested() const noexcept;
    [[nodiscard]] bool stop_possible() const noexcept;

    [[nodiscard]] friend bool operator==(const stop_token& lhs, const stop_token& rhs) noexcept;
    friend void swap(stop_token& lhs, stop_token& rhs) noexcept;
  };
}
```

## Subclauses

- [32.3.3.1 Constructors, copy, and assignment \[stoptoken.cons\]](stoptoken.cons.md)
- [32.3.3.2 Members \[stoptoken.mem\]](stoptoken.mem.md)
- [32.3.3.3 Non-member functions \[stoptoken.nonmembers\]](stoptoken.nonmembers.md)
