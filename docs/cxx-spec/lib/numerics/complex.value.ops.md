# 26.4.7 Value operations [complex.value.ops]

[Contents](../../README.md) · up: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · prev: [26.4.6 Non-member operations \[complex.ops\]](complex.ops.md) · next: [26.4.8 Transcendentals \[complex.transcendentals\]](complex.transcendentals.md)

```cpp
template<class T> constexpr T real(const complex<T>& x);
```

**1** *Returns:* `x.real()`.

```cpp
template<class T> constexpr T imag(const complex<T>& x);
```

**2** *Returns:* `x.imag()`.

```cpp
template<class T> T abs(const complex<T>& x);
```

**3** *Returns:* The magnitude of `x`.

```cpp
template<class T> T arg(const complex<T>& x);
```

**4** *Returns:* The phase angle of `x`, or `atan2(imag(x), real(x))`.

```cpp
template<class T> constexpr T norm(const complex<T>& x);
```

**5** *Returns:* The squared magnitude of `x`.

```cpp
template<class T> constexpr complex<T> conj(const complex<T>& x);
```

**6** *Returns:* The complex conjugate of `x`.

```cpp
template<class T> complex<T> proj(const complex<T>& x);
```

**7** *Returns:* The projection of `x` onto the Riemann sphere.

**8** *Remarks:* Behaves the same as the C function `cproj`. *See also:* ISO C 7.3.9.5

```cpp
template<class T> complex<T> polar(const T& rho, const T& theta = T());
```

**9** *Preconditions:* `rho` is non-negative and non-NaN. `theta` is finite.

**10** *Returns:* The `complex` value corresponding to a complex number whose magnitude is `rho` and whose phase angle is `theta`.
