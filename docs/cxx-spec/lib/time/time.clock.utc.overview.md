# 27.7.2.1 Overview [time.clock.utc.overview]

[Contents](../../README.md) · up: [27.7.2 Class `utc_clock` \[time.clock.utc\]](time.clock.utc.md) · prev: [27.7.2 Class `utc_clock` \[time.clock.utc\]](time.clock.utc.md) · next: [27.7.2.2 Member functions \[time.clock.utc.members\]](time.clock.utc.members.md)

```cpp
namespace std::chrono {
  class utc_clock {
  public:
    using rep                       = a signed arithmetic type;
    using period                    = ratio<unspecified, unspecified>;
    using duration                  = chrono::duration<rep, period>;
    using time_point                = chrono::time_point<utc_clock>;
    static constexpr bool is_steady = unspecified;

    static time_point now();

    template<class Duration>
      static sys_time<common_type_t<Duration, seconds>>
        to_sys(const utc_time<Duration>& t);
    template<class Duration>
      static utc_time<common_type_t<Duration, seconds>>
        from_sys(const sys_time<Duration>& t);
  };
}
```

**1** In contrast to `sys_time`, which does not take leap seconds into account, `utc_clock` and its associated `time_point`, `utc_time`, count time, including leap seconds, since 1970-01-01 00:00:00 UTC. [*Note 1*: The UTC time standard began on 1972-01-01 00:00:10 TAI. To measure time since this epoch instead, one can add/subtract the constant `sys_days{1972y/1/1} - sys_days{1970y/1/1}` (`63'072'000s`) from the `utc_time`. — *end note*] [*Example 1*: <br> `clock_cast<utc_clock>(sys_seconds{sys_days{1970y/January/1}}).time_since_epoch()` is `0s`. <br> `clock_cast<utc_clock>(sys_seconds{sys_days{2000y/January/1}}).time_since_epoch()` <br> is `946'684'822s`, which is `10'957 * 86'400s + 22s`. <br> — *end example*]

**2** `utc_clock` is not a *Cpp17TrivialClock* unless the implementation can guarantee that `utc_clock::now()` does not propagate an exception. [*Note 2*: `noexcept(from_sys(system_clock::now()))` is `false`. — *end note*]
