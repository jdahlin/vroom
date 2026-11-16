# 27.7.3.1 Overview [time.clock.tai.overview]

[Contents](../README.md) · up: [27.7.3 Class `tai_clock` \[time.clock.tai\]](time.clock.tai.md) · prev: [27.7.3 Class `tai_clock` \[time.clock.tai\]](time.clock.tai.md) · next: [27.7.3.2 Member functions \[time.clock.tai.members\]](time.clock.tai.members.md)

```cpp
namespace std::chrono {
  class tai_clock {
  public:
    using rep                       = a signed arithmetic type;
    using period                    = ratio<unspecified, unspecified>;
    using duration                  = chrono::duration<rep, period>;
    using time_point                = chrono::time_point<tai_clock>;
    static constexpr bool is_steady = unspecified;

    static time_point now();

    template<class Duration>
      static utc_time<common_type_t<Duration, seconds>>
        to_utc(const tai_time<Duration>&) noexcept;
    template<class Duration>
      static tai_time<common_type_t<Duration, seconds>>
        from_utc(const utc_time<Duration>&) noexcept;
  };
}
```

**1** The clock `tai_clock` measures seconds since 1958-01-01 00:00:00 and is offset 10s ahead of UTC at this date. That is, 1958-01-01 00:00:00 TAI is equivalent to 1957-12-31 23:59:50 UTC. Leap seconds are not inserted into TAI. Therefore every time a leap second is inserted into UTC, UTC shifts another second with respect to TAI. For example by 2000-01-01 there had been 22 positive and 0 negative leap seconds inserted so 2000-01-01 00:00:00 UTC is equivalent to 2000-01-01 00:00:32 TAI (22s plus the initial 10s offset).

**2** `tai_clock` is not a *Cpp17TrivialClock* unless the implementation can guarantee that `tai_clock::now()` does not propagate an exception. [*Note 1*: `noexcept(from_utc(utc_clock::now()))` is `false`. — *end note*]
