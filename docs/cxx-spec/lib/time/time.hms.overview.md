# 27.9.1 Overview [time.hms.overview]

[Contents](../../README.md) · up: [27.9 Class template `hh_mm_ss` \[time.hms\]](time.hms.md) · prev: [27.9 Class template `hh_mm_ss` \[time.hms\]](time.hms.md) · next: [27.9.2 Members \[time.hms.members\]](time.hms.members.md)

```cpp
namespace std::chrono {
  template<class Duration> class hh_mm_ss {
  public:
    static constexpr unsigned fractional_width = see below;
    using precision                            = see below;

    constexpr hh_mm_ss() noexcept : hh_mm_ss{Duration::zero()} {}
    constexpr explicit hh_mm_ss(Duration d);

    constexpr bool is_negative() const noexcept;
    constexpr chrono::hours hours() const noexcept;
    constexpr chrono::minutes minutes() const noexcept;
    constexpr chrono::seconds seconds() const noexcept;
    constexpr precision subseconds() const noexcept;

    constexpr explicit operator precision() const noexcept;
    constexpr precision to_duration() const noexcept;

  private:
    bool            is_neg;     // exposition only
    chrono::hours   h;          // exposition only
    chrono::minutes m;          // exposition only
    chrono::seconds s;          // exposition only
    precision       ss;         // exposition only
  };
}
```

**1** The `hh_mm_ss` class template splits a `duration` into a multi-field time structure *hours*:*minutes*:*seconds* and possibly *subseconds*, where *subseconds* will be a duration unit based on a non-positive power of 10. The `Duration` template parameter dictates the precision to which the time is split. A `hh_mm_ss` models negative durations with a distinct `is_negative` getter that returns `true` when the input duration is negative. The individual duration fields always return non-negative durations even when `is_negative()` indicates the structure is representing a negative duration.

**2** If `Duration` is not an instance of `duration`, the program is ill-formed.
