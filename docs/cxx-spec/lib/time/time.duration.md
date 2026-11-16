# 27.5 Class template `duration` [time.duration]

[Contents](../../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27.4.4 Class template `is_clock` \[time.traits.is.clock\]](time.traits.is.clock.md) · next: [27.5.1 Constructors \[time.duration.cons\]](time.duration.cons.md)

**1** A `duration` type measures time between two points in time (`time_point`s). A `duration` has a representation which holds a count of ticks and a tick period. The tick period is the amount of time which occurs from one tick to the next, in units of seconds. It is expressed as a rational constant using the template `ratio`.

```cpp
namespace std::chrono {
  template<class Rep, class Period = ratio<1>>
  class duration {
  public:
    using rep    = Rep;
    using period = typename Period::type;

  private:
    rep rep_;       // exposition only

  public:
    // [time.duration.cons], construct/copy/destroy
    constexpr duration() = default;
    template<class Rep2>
      constexpr explicit duration(const Rep2& r);
    template<class Rep2, class Period2>
      constexpr duration(const duration<Rep2, Period2>& d);
    ~duration() = default;
    duration(const duration&) = default;
    duration& operator=(const duration&) = default;

    // [time.duration.observer], observer
    constexpr rep count() const;

    // [time.duration.arithmetic], arithmetic
    constexpr common_type_t<duration> operator+() const;
    constexpr common_type_t<duration> operator-() const;
    constexpr duration& operator++();
    constexpr duration  operator++(int);
    constexpr duration& operator--();
    constexpr duration  operator--(int);

    constexpr duration& operator+=(const duration& d);
    constexpr duration& operator-=(const duration& d);

    constexpr duration& operator*=(const rep& rhs);
    constexpr duration& operator/=(const rep& rhs);
    constexpr duration& operator%=(const rep& rhs);
    constexpr duration& operator%=(const duration& rhs);

    // [time.duration.special], special values
    static constexpr duration zero() noexcept;
    static constexpr duration min() noexcept;
    static constexpr duration max() noexcept;
  };
}
```

**2** `Rep` shall be an arithmetic type or a class emulating an arithmetic type. If `duration` is instantiated with a `duration` type as the argument for the template parameter `Rep`, the program is ill-formed.

**3** If `Period` is not a specialization of `ratio`, the program is ill-formed. If `Period::num` is not positive, the program is ill-formed.

**4** Members of `duration` do not throw exceptions other than those thrown by the indicated operations on their representations.

**5** The defaulted copy constructor of duration shall be a constexpr function if and only if the required initialization of the member `rep_` for copy and move, respectively, would satisfy the requirements for a constexpr function.

**6** [*Example 1*:

```cpp
duration<long, ratio<60>> d0;       // holds a count of minutes using a long
duration<long long, milli> d1;      // holds a count of milliseconds using a long long
duration<double, ratio<1, 30>>  d2; // holds a count with a tick period of \frac{1}{30} of a second
                                    // (30 Hz) using a double
```

— *end example*]

## Subclauses

- [27.5.1 Constructors \[time.duration.cons\]](time.duration.cons.md)
- [27.5.2 Observer \[time.duration.observer\]](time.duration.observer.md)
- [27.5.3 Arithmetic \[time.duration.arithmetic\]](time.duration.arithmetic.md)
- [27.5.4 Special values \[time.duration.special\]](time.duration.special.md)
- [27.5.5 Non-member arithmetic \[time.duration.nonmember\]](time.duration.nonmember.md)
- [27.5.6 Comparisons \[time.duration.comparisons\]](time.duration.comparisons.md)
- [27.5.7 Conversions \[time.duration.cast\]](time.duration.cast.md)
- [27.5.8 Suffixes for duration literals \[time.duration.literals\]](time.duration.literals.md)
- [27.5.9 Algorithms \[time.duration.alg\]](time.duration.alg.md)
- [27.5.10 I/O \[time.duration.io\]](time.duration.io.md)
