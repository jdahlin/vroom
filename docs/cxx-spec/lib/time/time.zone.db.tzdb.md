# 27.11.2.1 Class `tzdb` [time.zone.db.tzdb]

[Contents](../../README.md) · up: [27.11.2 Time zone database \[time.zone.db\]](time.zone.db.md) · prev: [27.11.2 Time zone database \[time.zone.db\]](time.zone.db.md) · next: [27.11.2.2 Class `tzdb_list` \[time.zone.db.list\]](time.zone.db.list.md)

```cpp
namespace std::chrono {
  struct tzdb {
    string                 version;
    vector<time_zone>      zones;
    vector<time_zone_link> links;
    vector<leap_second>    leap_seconds;

    const time_zone* locate_zone(string_view tz_name) const;
    const time_zone* current_zone() const;
  };
}
```

**1** Each `vector` in a `tzdb` object is sorted to enable fast lookup.

```cpp
const time_zone* locate_zone(string_view tz_name) const;
```

**2** *Returns:*

- If `zones` contains an element `tz` for which `tz.name() == tz_name`, a pointer to `tz`;
- otherwise, if `links` contains an element `tz_l` for which `tz_l.name() == tz_name`, then a pointer to the element `tz` of `zones` for which `tz.name() == tz_l.target()`.

[*Note 1*: A `time_zone_link` specifies an alternative name for a `time_zone`. — *end note*]

**3** *Throws:* If a `const time_zone*` cannot be found as described in the *Returns:* clause, throws a `runtime_error`. [*Note 2*: On non-exceptional return, the return value is always a pointer to a valid `time_zone`. — *end note*]

```cpp
const time_zone* current_zone() const;
```

**4** *Returns:* A pointer to the time zone which the computer has set as its local time zone.
