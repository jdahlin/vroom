# 27.5.1 Constructors [time.duration.cons]

[Contents](../README.md) · up: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · prev: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · next: [27.5.2 Observer \[time.duration.observer\]](time.duration.observer.md)

```cpp
template<class Rep2>
  constexpr explicit duration(const Rep2& r);
```

**1** *Constraints:* `is_convertible_v<const Rep2&, rep>` is `true` and

- `treat_as_floating_point_v<rep>` is `true` or
- `treat_as_floating_point_v<Rep2>` is `false`.

[*Example 1*:

```cpp
duration<int, milli> d(3);          // OK
duration<int, milli> d(3.5);        // error
```

— *end example*]

**2** *Postconditions:* `count() == static_cast<rep>(r)`.

```cpp
template<class Rep2, class Period2>
  constexpr duration(const duration<Rep2, Period2>& d);
```

**3** *Constraints:* No overflow is induced in the conversion and `treat_as_floating_point_v<rep>` is `true` or both `ratio_divide<Period2, period>::den` is `1` and `treat_as_floating_point_v<Rep2>` is `false`. [*Note 1*: This requirement prevents implicit truncation error when converting between integral-based `duration` types. Such a construction could easily lead to confusion about the value of the `duration`. — *end note*] [*Example 2*:

```cpp
duration<int, milli> ms(3);
duration<int, micro> us = ms;       // OK
duration<int, milli> ms2 = us;      // error
```

— *end example*]

**4** *Effects:* Initializes `rep_` with `duration_cast<duration>(d).count()`.
