# 27.7.4.1 Overview [time.clock.gps.overview]

[Contents](../README.md) · up: [27.7.4 Class `gps_clock` \[time.clock.gps\]](time.clock.gps.md) · prev: [27.7.4 Class `gps_clock` \[time.clock.gps\]](time.clock.gps.md) · next: [27.7.4.2 Member functions \[time.clock.gps.members\]](time.clock.gps.members.md)

```cpp
namespace std::chrono {
  class gps_clock {
  public:
    using rep                       = a signed arithmetic type;
    using period                    = ratio<unspecified, unspecified>;
    using duration                  = chrono::duration<rep, period>;
    using time_point                = chrono::time_point<gps_clock>;
    static constexpr bool is_steady = unspecified;

    static time_point now();

    template<class Duration>
      static utc_time<common_type_t<Duration, seconds>>
        to_utc(const gps_time<Duration>&) noexcept;
    template<class Duration>
      static gps_time<common_type_t<Duration, seconds>>
        from_utc(const utc_time<Duration>&) noexcept;
  };
}
```

**1** The clock `gps_clock` measures seconds since the first Sunday of January, 1980 00:00:00 UTC. Leap seconds are not inserted into GPS. Therefore every time a leap second is inserted into UTC, UTC shifts another second with respect to GPS. Aside from the offset from `1958y/January/1` to `1980y/January/Sunday[1]`, GPS is behind TAI by 19s due to the 10s offset between 1958 and 1970 and the additional 9 leap seconds inserted between 1970 and 1980.

**2** `gps_clock` is not a *Cpp17TrivialClock* unless the implementation can guarantee that `gps_clock::now()` does not propagate an exception. [*Note 1*: `noexcept(from_utc(utc_clock::now()))` is `false`. — *end note*]
