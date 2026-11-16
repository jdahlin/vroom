# 27.5.3 Arithmetic [time.duration.arithmetic]

[Contents](../../README.md) · up: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · prev: [27.5.2 Observer \[time.duration.observer\]](time.duration.observer.md) · next: [27.5.4 Special values \[time.duration.special\]](time.duration.special.md)

```cpp
constexpr common_type_t<duration> operator+() const;
```

**1** *Returns:* `common_type_t<duration>(*this)`.

```cpp
constexpr common_type_t<duration> operator-() const;
```

**2** *Returns:* `common_type_t<duration>(-rep_)`.

```cpp
constexpr duration& operator++();
```

**3** *Effects:* Equivalent to: `++rep_`.

**4** *Returns:* `*this`.

```cpp
constexpr duration operator++(int);
```

**5** *Effects:* Equivalent to: `return duration(rep_++);`

```cpp
constexpr duration& operator--();
```

**6** *Effects:* Equivalent to: `--rep_`.

**7** *Returns:* `*this`.

```cpp
constexpr duration operator--(int);
```

**8** *Effects:* Equivalent to: `return duration(rep_--);`

```cpp
constexpr duration& operator+=(const duration& d);
```

**9** *Effects:* Equivalent to: `rep_ += d.count()`.

**10** *Returns:* `*this`.

```cpp
constexpr duration& operator-=(const duration& d);
```

**11** *Effects:* Equivalent to: `rep_ -= d.count()`.

**12** *Returns:* `*this`.

```cpp
constexpr duration& operator*=(const rep& rhs);
```

**13** *Effects:* Equivalent to: `rep_ *= rhs`.

**14** *Returns:* `*this`.

```cpp
constexpr duration& operator/=(const rep& rhs);
```

**15** *Effects:* Equivalent to: `rep_ /= rhs`.

**16** *Returns:* `*this`.

```cpp
constexpr duration& operator%=(const rep& rhs);
```

**17** *Effects:* Equivalent to: `rep_ %= rhs`.

**18** *Returns:* `*this`.

```cpp
constexpr duration& operator%=(const duration& rhs);
```

**19** *Effects:* Equivalent to: `rep_ %= rhs.count()`.

**20** *Returns:* `*this`.
