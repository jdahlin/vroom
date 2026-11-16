# 32.3.4 Class `stop_source` [stopsource]

[Contents](../README.md) · up: [32.3 Stop tokens \[thread.stoptoken\]](thread.stoptoken.md) · prev: [32.3.3.3 Non-member functions \[stoptoken.nonmembers\]](stoptoken.nonmembers.md) · next: [32.3.4.1 Constructors, copy, and assignment \[stopsource.cons\]](stopsource.cons.md)

**1** The class `stop_source` implements the semantics of making a stop request. A stop request made on a `stop_source` object is visible to all associated `stop_source` and `stop_token` ([[stoptoken]](stoptoken.md)) objects. Once a stop request has been made it cannot be withdrawn (a subsequent stop request has no effect).

```cpp
namespace std {
  // no-shared-stop-state indicator
  struct nostopstate_t {
    explicit nostopstate_t() = default;
  };
  inline constexpr nostopstate_t nostopstate{};

  class stop_source {
  public:
    // [stopsource.cons], constructors, copy, and assignment
    stop_source();
    explicit stop_source(nostopstate_t) noexcept;

    stop_source(const stop_source&) noexcept;
    stop_source(stop_source&&) noexcept;
    stop_source& operator=(const stop_source&) noexcept;
    stop_source& operator=(stop_source&&) noexcept;
    ~stop_source();
    void swap(stop_source&) noexcept;

    // [stopsource.mem], stop handling
    [[nodiscard]] stop_token get_token() const noexcept;
    [[nodiscard]] bool stop_possible() const noexcept;
    [[nodiscard]] bool stop_requested() const noexcept;
    bool request_stop() noexcept;

    [[nodiscard]] friend bool
      operator==(const stop_source& lhs, const stop_source& rhs) noexcept;
    friend void swap(stop_source& lhs, stop_source& rhs) noexcept;
  };
}
```

## Subclauses

- [32.3.4.1 Constructors, copy, and assignment \[stopsource.cons\]](stopsource.cons.md)
- [32.3.4.2 Members \[stopsource.mem\]](stopsource.mem.md)
- [32.3.4.3 Non-member functions \[stopsource.nonmembers\]](stopsource.nonmembers.md)
