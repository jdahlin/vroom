# 27.7.7 Class `high_resolution_clock` [time.clock.hires]

[Contents](../README.md) · up: [27.7 Clocks \[time.clock\]](time.clock.md) · prev: [27.7.6 Class `steady_clock` \[time.clock.steady\]](time.clock.steady.md) · next: [27.7.8 Local time \[time.clock.local\]](time.clock.local.md)

```cpp
namespace std::chrono {
  class high_resolution_clock {
  public:
    using rep        = unspecified;
    using period     = ratio<unspecified, unspecified>;
    using duration   = chrono::duration<rep, period>;
    using time_point = chrono::time_point<unspecified, duration>;
    static constexpr bool is_steady = unspecified;

    static time_point now() noexcept;
  };
}
```

**1** Objects of class `high_resolution_clock` represent clocks with the shortest tick period. `high_resolution_clock` may be a synonym for `system_clock` or `steady_clock`.
