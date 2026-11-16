# 27.11.7.1 Overview [time.zone.zonedtime.overview]

[Contents](../../README.md) · up: [27.11.7 Class template `zoned_time` \[time.zone.zonedtime\]](time.zone.zonedtime.md) · prev: [27.11.7 Class template `zoned_time` \[time.zone.zonedtime\]](time.zone.zonedtime.md) · next: [27.11.7.2 Constructors \[time.zone.zonedtime.ctor\]](time.zone.zonedtime.ctor.md)

```cpp
namespace std::chrono {
  template<class Duration, class TimeZonePtr = const time_zone*>
  class zoned_time {
  public:
    using duration = common_type_t<Duration, seconds>;

  private:
    TimeZonePtr        zone_;                   // exposition only
    sys_time<duration> tp_;                     // exposition only

    using traits = zoned_traits<TimeZonePtr>;   // exposition only

  public:
    zoned_time();
    zoned_time(const zoned_time&) = default;
    zoned_time& operator=(const zoned_time&) = default;

    zoned_time(const sys_time<Duration>& st);
    explicit zoned_time(TimeZonePtr z);
    explicit zoned_time(string_view name);

    template<class Duration2>
      zoned_time(const zoned_time<Duration2, TimeZonePtr>& zt);

    zoned_time(TimeZonePtr z,    const sys_time<Duration>& st);
    zoned_time(string_view name, const sys_time<Duration>& st);

    zoned_time(TimeZonePtr z,    const local_time<Duration>& tp);
    zoned_time(string_view name, const local_time<Duration>& tp);
    zoned_time(TimeZonePtr z,    const local_time<Duration>& tp, choose c);
    zoned_time(string_view name, const local_time<Duration>& tp, choose c);

    template<class Duration2, class TimeZonePtr2>
      zoned_time(TimeZonePtr z, const zoned_time<Duration2, TimeZonePtr2>& zt);
    template<class Duration2, class TimeZonePtr2>
      zoned_time(TimeZonePtr z, const zoned_time<Duration2, TimeZonePtr2>& zt, choose);

    template<class Duration2, class TimeZonePtr2>
      zoned_time(string_view name, const zoned_time<Duration2, TimeZonePtr2>& zt);
    template<class Duration2, class TimeZonePtr2>
      zoned_time(string_view name, const zoned_time<Duration2, TimeZonePtr2>& zt, choose);

    zoned_time& operator=(const sys_time<Duration>& st);
    zoned_time& operator=(const local_time<Duration>& ut);

    operator sys_time<duration>() const;
    explicit operator local_time<duration>() const;

    TimeZonePtr          get_time_zone()  const;
    local_time<duration> get_local_time() const;
    sys_time<duration>   get_sys_time()   const;
    sys_info             get_info()       const;
  };

  zoned_time() -> zoned_time<seconds>;

  template<class Duration>
    zoned_time(sys_time<Duration>)
      -> zoned_time<common_type_t<Duration, seconds>>;

  template<class TimeZonePtrOrName>
    using time-zone-representation =        // exposition only
      conditional_t<is_convertible_v<TimeZonePtrOrName, string_view>,
                    const time_zone*,
                    remove_cvref_t<TimeZonePtrOrName>>;

  template<class TimeZonePtrOrName>
    zoned_time(TimeZonePtrOrName&&)
      -> zoned_time<seconds, time-zone-representation<TimeZonePtrOrName>>;

  template<class TimeZonePtrOrName, class Duration>
    zoned_time(TimeZonePtrOrName&&, sys_time<Duration>)
      -> zoned_time<common_type_t<Duration, seconds>,
                    time-zone-representation<TimeZonePtrOrName>>;

  template<class TimeZonePtrOrName, class Duration>
    zoned_time(TimeZonePtrOrName&&, local_time<Duration>,
               choose = choose::earliest)
      -> zoned_time<common_type_t<Duration, seconds>,
                    time-zone-representation<TimeZonePtrOrName>>;

  template<class Duration, class TimeZonePtrOrName, class TimeZonePtr2>
    zoned_time(TimeZonePtrOrName&&, zoned_time<Duration, TimeZonePtr2>,
               choose = choose::earliest)
      -> zoned_time<common_type_t<Duration, seconds>,
                    time-zone-representation<TimeZonePtrOrName>>;
}
```

**1** `zoned_time` represents a logical pairing of a `time_zone` and a `time_point` with precision `Duration`. `zoned_time<Duration>` maintains the invariant that it always refers to a valid time zone and represents a point in time that exists and is not ambiguous in that time zone.

**2** If `Duration` is not a specialization of `chrono::duration`, the program is ill-formed.

**3** Every constructor of `zoned_time` that accepts a `string_view` as its first parameter does not participate in class template argument deduction ([[over.match.class.deduct]](../../over/over.match.class.deduct.md)).
