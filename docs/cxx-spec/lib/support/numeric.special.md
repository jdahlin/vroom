# 17.3.5.2 `numeric_limits` specializations [numeric.special]

[Contents](../../README.md) · up: [17.3.5 Class template `numeric_limits` \[numeric.limits\]](numeric.limits.md) · prev: [17.3.5.1 `numeric_limits` members \[numeric.limits.members\]](numeric.limits.members.md) · next: [17.3.6 Header `<climits>` synopsis \[climits.syn\]](climits.syn.md)

**1** All members shall be provided for all specializations. However, many values are only required to be meaningful under certain conditions (for example, `epsilon()` is only meaningful if `is_integer` is `false`). Any value that is not “meaningful” shall be set to 0 or `false`.

**2** [*Example 1*:

```cpp
namespace std {
  template<> class numeric_limits<float> {
  public:
    static constexpr bool is_specialized = true;

    static constexpr float min() noexcept { return 1.17549435E-38F; }
    static constexpr float max() noexcept { return 3.40282347E+38F; }
    static constexpr float lowest() noexcept { return -3.40282347E+38F; }

    static constexpr int digits   = 24;
    static constexpr int digits10 =  6;
    static constexpr int max_digits10 =  9;

    static constexpr bool is_signed  = true;
    static constexpr bool is_integer = false;
    static constexpr bool is_exact   = false;

    static constexpr int radix = 2;
    static constexpr float epsilon() noexcept     { return 1.19209290E-07F; }
    static constexpr float round_error() noexcept { return 0.5F; }

    static constexpr int min_exponent   = -125;
    static constexpr int min_exponent10 = - 37;
    static constexpr int max_exponent   = +128;
    static constexpr int max_exponent10 = + 38;

    static constexpr bool has_infinity             = true;
    static constexpr bool has_quiet_NaN            = true;
    static constexpr bool has_signaling_NaN        = true;
    static constexpr float_denorm_style has_denorm = denorm_absent;
    static constexpr bool has_denorm_loss          = false;

    static constexpr float infinity()      noexcept { return value; }
    static constexpr float quiet_NaN()     noexcept { return value; }
    static constexpr float signaling_NaN() noexcept { return value; }
    static constexpr float denorm_min()    noexcept { return min(); }

    static constexpr bool is_iec559  = true;
    static constexpr bool is_bounded = true;
    static constexpr bool is_modulo  = false;
    static constexpr bool traps      = true;
    static constexpr bool tinyness_before = true;

    static constexpr float_round_style round_style = round_to_nearest;
  };
}
```

— *end example*]

**3** The specialization for `bool` shall be provided as follows:

```cpp
namespace std {
   template<> class numeric_limits<bool> {
   public:
     static constexpr bool is_specialized = true;
     static constexpr bool min() noexcept { return false; }
     static constexpr bool max() noexcept { return true; }
     static constexpr bool lowest() noexcept { return false; }

     static constexpr int  digits = 1;
     static constexpr int  digits10 = 0;
     static constexpr int  max_digits10 = 0;

     static constexpr bool is_signed = false;
     static constexpr bool is_integer = true;
     static constexpr bool is_exact = true;
     static constexpr int  radix = 2;
     static constexpr bool epsilon() noexcept { return 0; }
     static constexpr bool round_error() noexcept { return 0; }

     static constexpr int  min_exponent = 0;
     static constexpr int  min_exponent10 = 0;
     static constexpr int  max_exponent = 0;
     static constexpr int  max_exponent10 = 0;

     static constexpr bool has_infinity = false;
     static constexpr bool has_quiet_NaN = false;
     static constexpr bool has_signaling_NaN = false;
     static constexpr float_denorm_style has_denorm = denorm_absent;
     static constexpr bool has_denorm_loss = false;
     static constexpr bool infinity() noexcept { return 0; }
     static constexpr bool quiet_NaN() noexcept { return 0; }
     static constexpr bool signaling_NaN() noexcept { return 0; }
     static constexpr bool denorm_min() noexcept { return 0; }

     static constexpr bool is_iec559 = false;
     static constexpr bool is_bounded = true;
     static constexpr bool is_modulo = false;

     static constexpr bool traps = false;
     static constexpr bool tinyness_before = false;
     static constexpr float_round_style round_style = round_toward_zero;
   };
}
```
