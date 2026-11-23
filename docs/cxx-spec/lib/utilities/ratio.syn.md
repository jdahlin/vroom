# 20.16.2 Header `<ratio>` synopsis [ratio.syn]

[Contents](../../README.md) · up: [20.16 Compile-time rational arithmetic \[ratio\]](ratio.md) · prev: [20.16.1 In general \[ratio.general\]](ratio.general.md) · next: [20.16.3 Class template `ratio` \[ratio.ratio\]](ratio.ratio.md)

```cpp
namespace std {
  // [ratio.ratio], class template ratio
  template<intmax_t N, intmax_t D = 1> class ratio;

  // [ratio.arithmetic], ratio arithmetic
  template<class R1, class R2> using ratio_add = see below;
  template<class R1, class R2> using ratio_subtract = see below;
  template<class R1, class R2> using ratio_multiply = see below;
  template<class R1, class R2> using ratio_divide = see below;

  // [ratio.comparison], ratio comparison
  template<class R1, class R2> struct ratio_equal;
  template<class R1, class R2> struct ratio_not_equal;
  template<class R1, class R2> struct ratio_less;
  template<class R1, class R2> struct ratio_less_equal;
  template<class R1, class R2> struct ratio_greater;
  template<class R1, class R2> struct ratio_greater_equal;

  template<class R1, class R2>
    inline constexpr bool ratio_equal_v = ratio_equal<R1, R2>::value;
  template<class R1, class R2>
    inline constexpr bool ratio_not_equal_v = ratio_not_equal<R1, R2>::value;
  template<class R1, class R2>
    inline constexpr bool ratio_less_v = ratio_less<R1, R2>::value;
  template<class R1, class R2>
    inline constexpr bool ratio_less_equal_v = ratio_less_equal<R1, R2>::value;
  template<class R1, class R2>
    inline constexpr bool ratio_greater_v = ratio_greater<R1, R2>::value;
  template<class R1, class R2>
    inline constexpr bool ratio_greater_equal_v = ratio_greater_equal<R1, R2>::value;

  // [ratio.si], convenience SI typedefs
  using yocto = ratio<1, 1'000'000'000'000'000'000'000'000>;  // see below
  using zepto = ratio<1,     1'000'000'000'000'000'000'000>;  // see below
  using atto  = ratio<1,         1'000'000'000'000'000'000>;
  using femto = ratio<1,             1'000'000'000'000'000>;
  using pico  = ratio<1,                 1'000'000'000'000>;
  using nano  = ratio<1,                     1'000'000'000>;
  using micro = ratio<1,                         1'000'000>;
  using milli = ratio<1,                             1'000>;
  using centi = ratio<1,                               100>;
  using deci  = ratio<1,                                10>;
  using deca  = ratio<                               10, 1>;
  using hecto = ratio<                              100, 1>;
  using kilo  = ratio<                            1'000, 1>;
  using mega  = ratio<                        1'000'000, 1>;
  using giga  = ratio<                    1'000'000'000, 1>;
  using tera  = ratio<                1'000'000'000'000, 1>;
  using peta  = ratio<            1'000'000'000'000'000, 1>;
  using exa   = ratio<        1'000'000'000'000'000'000, 1>;
  using zetta = ratio<    1'000'000'000'000'000'000'000, 1>;  // see below
  using yotta = ratio<1'000'000'000'000'000'000'000'000, 1>;  // see below
}
```
