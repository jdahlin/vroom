# 27.9.2 Members [time.hms.members]

[Contents](../../README.md) · up: [27.9 Class template `hh_mm_ss` \[time.hms\]](time.hms.md) · prev: [27.9.1 Overview \[time.hms.overview\]](time.hms.overview.md) · next: [27.9.3 Non-members \[time.hms.nonmembers\]](time.hms.nonmembers.md)

```cpp
static constexpr unsigned fractional_width = see below;
```

**1** `fractional_width` is the number of fractional decimal digits represented by `precision`. `fractional_width` has the value of the smallest possible integer in the range `[0, 18]` such that `precision` will exactly represent all values of `Duration`. If no such value of `fractional_width` exists, then `fractional_width` is 6. [*Example 1*: See [Table 98](time.hms.members.md#tab:time.hms.width) for some durations, the resulting `fractional_width`, and the formatted fractional second output of `Duration{1}`.

<a id="tab:time.hms.width"></a>
**Table 98: Examples for `fractional_width`** [tab:time.hms.width]

| Duration | `fractional_width` | Formatted fractional second output |
| --- | --- | --- |
| `hours`, `minutes`, and `seconds` | `0` |  |
| `milliseconds` | `3` | `0.001` |
| `microseconds` | `6` | `0.000001` |
| `nanoseconds` | `9` | `0.000000001` |
| `duration<int, ratio<1, 2>>` | `1` | `0.5` |
| `duration<int, ratio<1, 3>>` | `6` | `0.333333` |
| `duration<int, ratio<1, 4>>` | `2` | `0.25` |
| `duration<int, ratio<1, 5>>` | `1` | `0.2` |
| `duration<int, ratio<1, 6>>` | `6` | `0.166666` |
| `duration<int, ratio<1, 7>>` | `6` | `0.142857` |
| `duration<int, ratio<1, 8>>` | `3` | `0.125` |
| `duration<int, ratio<1, 9>>` | `6` | `0.111111` |
| `duration<int, ratio<1, 10>>` | `1` | `0.1` |
| `duration<int, ratio<756, 625>>` | `4` | `0.2096` |

— *end example*]

```cpp
using precision = see below;
```

**2** `precision` is

```cpp
duration<common_type_t<Duration::rep, seconds::rep>, ratio<1, 10^()fractional_width>>
```

```cpp
constexpr explicit hh_mm_ss(Duration d);
```

**3** *Effects:* Constructs an object of type `hh_mm_ss` which represents the `Duration d` with precision `precision`.

- Initializes `is_neg` with `d < Duration::zero()`.
- Initializes `h` with `duration_cast<chrono::hours>(abs(d))`.
- Initializes `m` with `duration_cast<chrono::minutes>(abs(d) - hours())`.
- Initializes `s` with `duration_cast<chrono::seconds>(abs(d) - hours() - minutes())`.
- If `treat_as_floating_point_v<precision::rep>` is `true`, initializes `ss` with `abs(d) - hours() - minutes() - seconds()`. Otherwise, initializes `ss` with `duration_cast<precision>(abs(d) - hours() - minutes() - seconds())`.

[*Note 1*: When `precision::rep` is integral and `precision::period` is `ratio<1>`, `subseconds()` always returns a value equal to `0s`. — *end note*]

**4** *Postconditions:* If `treat_as_floating_point_v<precision::rep>` is `true`, `to_duration()` returns `d`, otherwise `to_duration()` returns `duration_cast<precision>(d)`.

```cpp
constexpr bool is_negative() const noexcept;
```

**5** *Returns:* `is_neg`.

```cpp
constexpr chrono::hours hours() const noexcept;
```

**6** *Returns:* `h`.

```cpp
constexpr chrono::minutes minutes() const noexcept;
```

**7** *Returns:* `m`.

```cpp
constexpr chrono::seconds seconds() const noexcept;
```

**8** *Returns:* `s`.

```cpp
constexpr precision subseconds() const noexcept;
```

**9** *Returns:* `ss`.

```cpp
constexpr precision to_duration() const noexcept;
```

**10** *Returns:* If `is_neg`, returns `-(h + m + s + ss)`, otherwise returns `h + m + s + ss`.

```cpp
constexpr explicit operator precision() const noexcept;
```

**11** *Returns:* `to_duration()`.
