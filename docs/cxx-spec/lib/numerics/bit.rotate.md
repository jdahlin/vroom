# 26.5.5 Rotating [bit.rotate]

[Contents](../../README.md) · up: [26.5 Bit manipulation \[bit\]](bit.md) · prev: [26.5.4 Integral powers of 2 \[bit.pow.two\]](bit.pow.two.md) · next: [26.5.6 Counting \[bit.count\]](bit.count.md)

In the following descriptions, let `N` denote `numeric_limits<T>::digits`.

```cpp
template<class T>
  [[nodiscard]] constexpr T rotl(T x, int s) noexcept;
```

**1** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../../basic/basic.fundamental.md)).

**2** Let `r` be `s % N`.

**3** *Returns:* If `r` is `0`, `x`; if `r` is positive, `(x << r) | (x >> (N - r))`; if `r` is negative, `rotr(x, -r)`.

```cpp
template<class T>
  [[nodiscard]] constexpr T rotr(T x, int s) noexcept;
```

**4** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../../basic/basic.fundamental.md)).

**5** Let `r` be `s % N`.

**6** *Returns:* If `r` is `0`, `x`; if `r` is positive, `(x >> r) | (x << (N - r))`; if `r` is negative, `rotl(x, -r)`.
