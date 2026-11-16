# 27.7.6 Class `steady_clock` [time.clock.steady]

[Contents](../README.md) · up: [27.7 Clocks \[time.clock\]](time.clock.md) · prev: [27.7.5.3 Non-member functions \[time.clock.file.nonmembers\]](time.clock.file.nonmembers.md) · next: [27.7.7 Class `high_resolution_clock` \[time.clock.hires\]](time.clock.hires.md)

```cpp
namespace std::chrono {
  class steady_clock {
  public:
    using rep        = unspecified;
    using period     = ratio<unspecified, unspecified>;
    using duration   = chrono::duration<rep, period>;
    using time_point = chrono::time_point<unspecified, duration>;
    static constexpr bool is_steady = true;

    static time_point now() noexcept;
  };
}
```

**1** Objects of class `steady_clock` represent clocks for which values of `time_point` never decrease as physical time advances and for which values of `time_point` advance at a steady rate relative to real time. That is, the clock may not be adjusted.
