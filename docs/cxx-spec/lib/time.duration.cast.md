# 27.5.7 Conversions [time.duration.cast]

[Contents](../README.md) · up: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · prev: [27.5.6 Comparisons \[time.duration.comparisons\]](time.duration.comparisons.md) · next: [27.5.8 Suffixes for duration literals \[time.duration.literals\]](time.duration.literals.md)

```cpp
template<class ToDuration, class Rep, class Period>
  constexpr ToDuration duration_cast(const duration<Rep, Period>& d);
```

**1** *Constraints:* `ToDuration` is a specialization of `duration`.

**2** *Returns:* Let `CF` be `ratio_divide<Period, typename ToDuration::period>`, and `CR` be `common_type<typename ToDuration::rep, Rep, intmax_t>::type`.

- If `CF::num == 1` and `CF::den == 1`, returns

  ```cpp
  ToDuration(static_cast<typename ToDuration::rep>(d.count()))
  ```

- otherwise, if `CF::num != 1` and `CF::den == 1`, returns

  ```cpp
  ToDuration(static_cast<typename ToDuration::rep>(
    static_cast<CR>(d.count()) * static_cast<CR>(CF::num)))
  ```

- otherwise, if `CF::num == 1` and `CF::den != 1`, returns

  ```cpp
  ToDuration(static_cast<typename ToDuration::rep>(
    static_cast<CR>(d.count()) / static_cast<CR>(CF::den)))
  ```

- otherwise, returns

  ```cpp
  ToDuration(static_cast<typename ToDuration::rep>(
    static_cast<CR>(d.count()) * static_cast<CR>(CF::num) / static_cast<CR>(CF::den)))
  ```

**3** [*Note 1*: This function does not use any implicit conversions; all conversions are done with `static_cast`. It avoids multiplications and divisions when it is known at compile time that one or more arguments is 1. Intermediate computations are carried out in the widest representation and only converted to the destination representation at the final step. — *end note*]

```cpp
template<class ToDuration, class Rep, class Period>
  constexpr ToDuration floor(const duration<Rep, Period>& d);
```

**4** *Constraints:* `ToDuration` is a specialization of `duration`.

**5** *Returns:* The greatest result `t` representable in `ToDuration` for which `t <= d`.

```cpp
template<class ToDuration, class Rep, class Period>
  constexpr ToDuration ceil(const duration<Rep, Period>& d);
```

**6** *Constraints:* `ToDuration` is a specialization of `duration`.

**7** *Returns:* The least result `t` representable in `ToDuration` for which `t >= d`.

```cpp
template<class ToDuration, class Rep, class Period>
  constexpr ToDuration round(const duration<Rep, Period>& d);
```

**8** *Constraints:* `ToDuration` is a specialization of `duration` and `treat_as_floating_point_v<typename ToDuration::rep>` is `false`.

**9** *Returns:* The value of `ToDuration` that is closest to `d`. If there are two closest values, then return the value `t` for which `t % 2 == 0`.
