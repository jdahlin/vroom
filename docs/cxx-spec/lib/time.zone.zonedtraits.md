# 27.11.6 Class template `zoned_traits` [time.zone.zonedtraits]

[Contents](../README.md) · up: [27.11 Time zones \[time.zone\]](time.zone.md) · prev: [27.11.5.3 Non-member functions \[time.zone.nonmembers\]](time.zone.nonmembers.md) · next: [27.11.7 Class template `zoned_time` \[time.zone.zonedtime\]](time.zone.zonedtime.md)

```cpp
namespace std::chrono {
  template<class T> struct zoned_traits {};
}
```

**1** `zoned_traits` provides a means for customizing the behavior of `zoned_time<Duration, TimeZonePtr>` for the `zoned_time` default constructor, and constructors taking `string_view`. A specialization for `const time_zone*` is provided by the implementation:

```cpp
namespace std::chrono {
  template<> struct zoned_traits<const time_zone*> {
    static const time_zone* default_zone();
    static const time_zone* locate_zone(string_view name);
  };
}
```

```cpp
static const time_zone* default_zone();
```

**2** *Returns:* `std::chrono::locate_zone("UTC")`.

```cpp
static const time_zone* locate_zone(string_view name);
```

**3** *Returns:* `std::chrono::locate_zone(name)`.
