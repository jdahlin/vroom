# 27.8.12.2 Member functions [time.cal.mwdlast.members]

[Contents](../README.md) · up: [27.8.12 Class `month_weekday_last` \[time.cal.mwdlast\]](time.cal.mwdlast.md) · prev: [27.8.12.1 Overview \[time.cal.mwdlast.overview\]](time.cal.mwdlast.overview.md) · next: [27.8.12.3 Non-member functions \[time.cal.mwdlast.nonmembers\]](time.cal.mwdlast.nonmembers.md)

```cpp
constexpr month_weekday_last(const chrono::month& m,
                             const chrono::weekday_last& wdl) noexcept;
```

**1** *Effects:* Initializes `m_` with `m`, and `wdl_` with `wdl`.

```cpp
constexpr chrono::month month() const noexcept;
```

**2** *Returns:* `m_`.

```cpp
constexpr chrono::weekday_last weekday_last() const noexcept;
```

**3** *Returns:* `wdl_`.

```cpp
constexpr bool ok() const noexcept;
```

**4** *Returns:* `m_.ok() && wdl_.ok()`.
