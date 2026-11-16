# 27.7.1.1 Overview [time.clock.system.overview]

[Contents](../../README.md) · up: [27.7.1 Class `system_clock` \[time.clock.system\]](time.clock.system.md) · prev: [27.7.1 Class `system_clock` \[time.clock.system\]](time.clock.system.md) · next: [27.7.1.2 Members \[time.clock.system.members\]](time.clock.system.members.md)

```cpp
namespace std::chrono {
  class system_clock {
  public:
    using rep        = see below;
    using period     = ratio<unspecified, unspecified>;
    using duration   = chrono::duration<rep, period>;
    using time_point = chrono::time_point<system_clock>;
    static constexpr bool is_steady = unspecified;

    static time_point now() noexcept;

    // mapping to/from C type time_t
    static time_t      to_time_t  (const time_point& t) noexcept;
    static time_point  from_time_t(time_t t) noexcept;
  };
}
```

**1** Objects of type `system_clock` represent wall clock time from the system-wide realtime clock. Objects of type `sys_time<Duration>` measure time since 1970-01-01 00:00:00 UTC excluding leap seconds. This measure is commonly referred to as *Unix time*. This measure facilitates an efficient mapping between `sys_time` and calendar types ([[time.cal]](time.cal.md)). [*Example 1*: <br> `sys_seconds{sys_days{1970y/January/1}}.time_since_epoch()` is `0s`. <br> `sys_seconds{sys_days{2000y/January/1}}.time_since_epoch()` is `946'684'800s`, which is `10'957 * 86'400s`. <br> — *end example*]
