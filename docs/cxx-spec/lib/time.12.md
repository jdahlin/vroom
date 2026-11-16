# 27.10 12/24 hours functions [time.12]

[Contents](../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27.9.3 Non-members \[time.hms.nonmembers\]](time.hms.nonmembers.md) · next: [27.11 Time zones \[time.zone\]](time.zone.md)

**1** These functions aid in translating between a 12h format time of day and a 24h format time of day.

```cpp
constexpr bool is_am(const hours& h) noexcept;
```

**2** *Returns:* `0h <= h && h <= 11h`.

```cpp
constexpr bool is_pm(const hours& h) noexcept;
```

**3** *Returns:* `12h <= h && h <= 23h`.

```cpp
constexpr hours make12(const hours& h) noexcept;
```

**4** *Returns:* The 12-hour equivalent of `h` in the range `[1h, 12h]`. If `h` is not in the range `[0h, 23h]`, the value returned is unspecified.

```cpp
constexpr hours make24(const hours& h, bool is_pm) noexcept;
```

**5** *Returns:* If `is_pm` is `false`, returns the 24-hour equivalent of `h` in the range `[0h, 11h]`, assuming `h` represents an ante meridiem hour. Otherwise, returns the 24-hour equivalent of `h` in the range `[12h, 23h]`, assuming `h` represents a post meridiem hour. If `h` is not in the range `[1h, 12h]`, the value returned is unspecified.
