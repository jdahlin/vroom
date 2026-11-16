# 26.8.4 Linear interpolation [c.math.lerp]

[Contents](../README.md) · up: [26.8 Mathematical functions for floating-point types \[c.math\]](c.math.md) · prev: [26.8.3 Three-dimensional hypotenuse \[c.math.hypot3\]](c.math.hypot3.md) · next: [26.8.5 Classification / comparison functions \[c.math.fpclass\]](c.math.fpclass.md)

```cpp
constexpr float lerp(float a, float b, float t) noexcept;
constexpr double lerp(double a, double b, double t) noexcept;
constexpr long double lerp(long double a, long double b, long double t) noexcept;
```

**1** *Returns:* *a*+*t*(*b*-*a*).

**2** *Remarks:* Let `r` be the value returned. If `isfinite(a) && isfinite(b)`, then:

- If `t == 0`, then `r == a`.
- If `t == 1`, then `r == b`.
- If `t >= 0 && t <= 1`, then `isfinite(r)`.
- If `isfinite(t) && a == b`, then `r == a`.
- If `isfinite(t) || !isnan(t) && b-a != 0`, then `!isnan(r)`.

Let `CMP(x,y)` be `1` if `x > y`, `-1` if `x < y`, and `0` otherwise. For any `t1` and `t2`, the product of `CMP(lerp(a, b, t2), lerp(a, b, t1))`, `CMP(t2, t1)`, and `CMP(b, a)` is non-negative.
