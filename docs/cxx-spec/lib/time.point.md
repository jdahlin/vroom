# 27.6 Class template `time_point` [time.point]

[Contents](../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27.5.10 I/O \[time.duration.io\]](time.duration.io.md) · next: [27.6.1 Constructors \[time.point.cons\]](time.point.cons.md)

```cpp
namespace std::chrono {
  template<class Clock, class Duration = typename Clock::duration>
  class time_point {
  public:
    using clock    = Clock;
    using duration = Duration;
    using rep      = typename duration::rep;
    using period   = typename duration::period;

  private:
    duration d_;                                                // exposition only

  public:
    // [time.point.cons], construct
    constexpr time_point();                                     // has value epoch
    constexpr explicit time_point(const duration& d);           // same as time_point() + d
    template<class Duration2>
      constexpr time_point(const time_point<clock, Duration2>& t);

    // [time.point.observer], observer
    constexpr duration time_since_epoch() const;

    // [time.point.arithmetic], arithmetic
    constexpr time_point& operator++();
    constexpr time_point operator++(int);
    constexpr time_point& operator--();
    constexpr time_point operator--(int);
    constexpr time_point& operator+=(const duration& d);
    constexpr time_point& operator-=(const duration& d);

    // [time.point.special], special values
    static constexpr time_point min() noexcept;
    static constexpr time_point max() noexcept;
  };
}
```

**1** `Clock` shall either meet the *Cpp17Clock* requirements ([[time.clock.req]](time.clock.req.md)) or be the type `local_t`.

**2** If `Duration` is not an instance of `duration`, the program is ill-formed.

## Subclauses

- [27.6.1 Constructors \[time.point.cons\]](time.point.cons.md)
- [27.6.2 Observer \[time.point.observer\]](time.point.observer.md)
- [27.6.3 Arithmetic \[time.point.arithmetic\]](time.point.arithmetic.md)
- [27.6.4 Special values \[time.point.special\]](time.point.special.md)
- [27.6.5 Non-member arithmetic \[time.point.nonmember\]](time.point.nonmember.md)
- [27.6.6 Comparisons \[time.point.comparisons\]](time.point.comparisons.md)
- [27.6.7 Conversions \[time.point.cast\]](time.point.cast.md)
