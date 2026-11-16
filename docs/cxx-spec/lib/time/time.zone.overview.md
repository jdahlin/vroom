# 27.11.5.1 Overview [time.zone.overview]

[Contents](../../README.md) · up: [27.11.5 Class `time_zone` \[time.zone.timezone\]](time.zone.timezone.md) · prev: [27.11.5 Class `time_zone` \[time.zone.timezone\]](time.zone.timezone.md) · next: [27.11.5.2 Member functions \[time.zone.members\]](time.zone.members.md)

```cpp
namespace std::chrono {
  class time_zone {
  public:
    time_zone(time_zone&&) = default;
    time_zone& operator=(time_zone&&) = default;

    // unspecified additional constructors

    string_view name() const noexcept;

    template<class Duration> sys_info   get_info(const sys_time<Duration>& st)   const;
    template<class Duration> local_info get_info(const local_time<Duration>& tp) const;

    template<class Duration>
      sys_time<common_type_t<Duration, seconds>>
        to_sys(const local_time<Duration>& tp) const;

    template<class Duration>
      sys_time<common_type_t<Duration, seconds>>
        to_sys(const local_time<Duration>& tp, choose z) const;

    template<class Duration>
      local_time<common_type_t<Duration, seconds>>
        to_local(const sys_time<Duration>& tp) const;
  };
}
```

**1** A `time_zone` represents all time zone transitions for a specific geographic area. `time_zone` construction is unspecified, and performed as part of database initialization. [*Note 1*: `const time_zone` objects can be accessed via functions such as `locate_zone`. — *end note*]
