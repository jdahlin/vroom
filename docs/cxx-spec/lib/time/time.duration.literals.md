# 27.5.8 Suffixes for duration literals [time.duration.literals]

[Contents](../../README.md) · up: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · prev: [27.5.7 Conversions \[time.duration.cast\]](time.duration.cast.md) · next: [27.5.9 Algorithms \[time.duration.alg\]](time.duration.alg.md)

**1** This subclause describes literal suffixes for constructing duration literals. The suffixes `h`, `min`, `s`, `ms`, `us`, `ns` denote duration values of the corresponding types `hours`, `minutes`, `seconds`, `milliseconds`, `microseconds`, and `nanoseconds` respectively if they are applied to *integer-literal*s.

**2** If any of these suffixes are applied to a *floating-point-literal* the result is a `chrono::duration` literal with an unspecified floating-point representation.

**3** If any of these suffixes are applied to an *integer-literal* and the resulting `chrono::duration` value cannot be represented in the result type because of overflow, the program is ill-formed.

**4** [*Example 1*: The following code shows some duration literals.

```cpp
using namespace std::chrono_literals;
auto constexpr aday=24h;
auto constexpr lesson=45min;
auto constexpr halfanhour=0.5h;
```

— *end example*]

```cpp
constexpr chrono::hours                                 operator""h(unsigned long long hours);
constexpr chrono::duration<unspecified, ratio<3600, 1>> operator""h(long double hours);
```

**5** *Returns:* A `duration` literal representing `hours` hours.

```cpp
constexpr chrono::minutes                             operator""min(unsigned long long minutes);
constexpr chrono::duration<unspecified, ratio<60, 1>> operator""min(long double minutes);
```

**6** *Returns:* A `duration` literal representing `minutes` minutes.

```cpp
constexpr chrono::seconds               operator""s(unsigned long long sec);
constexpr chrono::duration<unspecified> operator""s(long double sec);
```

**7** *Returns:* A `duration` literal representing `sec` seconds.

**8** [*Note 1*: The same suffix `s` is used for `basic_string` but there is no conflict, since duration suffixes apply to numbers and string literal suffixes apply to character array literals. — *end note*]

```cpp
constexpr chrono::milliseconds                 operator""ms(unsigned long long msec);
constexpr chrono::duration<unspecified, milli> operator""ms(long double msec);
```

**9** *Returns:* A `duration` literal representing `msec` milliseconds.

```cpp
constexpr chrono::microseconds                 operator""us(unsigned long long usec);
constexpr chrono::duration<unspecified, micro> operator""us(long double usec);
```

**10** *Returns:* A `duration` literal representing `usec` microseconds.

```cpp
constexpr chrono::nanoseconds                 operator""ns(unsigned long long nsec);
constexpr chrono::duration<unspecified, nano> operator""ns(long double nsec);
```

**11** *Returns:* A `duration` literal representing `nsec` nanoseconds.
