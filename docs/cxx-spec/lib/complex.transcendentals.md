# 26.4.8 Transcendentals [complex.transcendentals]

[Contents](../README.md) · up: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · prev: [26.4.7 Value operations \[complex.value.ops\]](complex.value.ops.md) · next: [26.4.9 Additional overloads \[cmplx.over\]](cmplx.over.md)

```cpp
template<class T> complex<T> acos(const complex<T>& x);
```

**1** *Returns:* The complex arc cosine of `x`.

**2** *Remarks:* Behaves the same as the C function `cacos`. *See also:* ISO C 7.3.5.1

```cpp
template<class T> complex<T> asin(const complex<T>& x);
```

**3** *Returns:* The complex arc sine of `x`.

**4** *Remarks:* Behaves the same as the C function `casin`. *See also:* ISO C 7.3.5.2

```cpp
template<class T> complex<T> atan(const complex<T>& x);
```

**5** *Returns:* The complex arc tangent of `x`.

**6** *Remarks:* Behaves the same as the C function `catan`. *See also:* ISO C 7.3.5.3

```cpp
template<class T> complex<T> acosh(const complex<T>& x);
```

**7** *Returns:* The complex arc hyperbolic cosine of `x`.

**8** *Remarks:* Behaves the same as the C function `cacosh`. *See also:* ISO C 7.3.6.1

```cpp
template<class T> complex<T> asinh(const complex<T>& x);
```

**9** *Returns:* The complex arc hyperbolic sine of `x`.

**10** *Remarks:* Behaves the same as the C function `casinh`. *See also:* ISO C 7.3.6.2

```cpp
template<class T> complex<T> atanh(const complex<T>& x);
```

**11** *Returns:* The complex arc hyperbolic tangent of `x`.

**12** *Remarks:* Behaves the same as the C function `catanh`. *See also:* ISO C 7.3.6.3

```cpp
template<class T> complex<T> cos(const complex<T>& x);
```

**13** *Returns:* The complex cosine of `x`.

```cpp
template<class T> complex<T> cosh(const complex<T>& x);
```

**14** *Returns:* The complex hyperbolic cosine of `x`.

```cpp
template<class T> complex<T> exp(const complex<T>& x);
```

**15** *Returns:* The complex base-*e* exponential of `x`.

```cpp
template<class T> complex<T> log(const complex<T>& x);
```

**16** *Returns:* The complex natural (base-*e*) logarithm of `x`. For all `x`, `imag(log(x))` lies in the interval `[-π, π]`. [*Note 1*: The semantics of this function are intended to be the same in C++ as they are for `clog` in C. — *end note*]

**17** *Remarks:* The branch cuts are along the negative real axis.

```cpp
template<class T> complex<T> log10(const complex<T>& x);
```

**18** *Returns:* The complex common (base-10) logarithm of `x`, defined as `log(x) / log(10)`.

**19** *Remarks:* The branch cuts are along the negative real axis.

```cpp
template<class T> complex<T> pow(const complex<T>& x, const complex<T>& y);
template<class T> complex<T> pow(const complex<T>& x, const T& y);
template<class T> complex<T> pow(const T& x, const complex<T>& y);
```

**20** *Returns:* The complex power of base `x` raised to the `y`<sup></sup>*th* power, defined as `exp(y * log(x))`. The value returned for `pow(0, 0)` is implementation-defined.

**21** *Remarks:* The branch cuts are along the negative real axis.

```cpp
template<class T> complex<T> sin(const complex<T>& x);
```

**22** *Returns:* The complex sine of `x`.

```cpp
template<class T> complex<T> sinh(const complex<T>& x);
```

**23** *Returns:* The complex hyperbolic sine of `x`.

```cpp
template<class T> complex<T> sqrt(const complex<T>& x);
```

**24** *Returns:* The complex square root of `x`, in the range of the right half-plane. [*Note 2*: The semantics of this function are intended to be the same in C++ as they are for `csqrt` in C. — *end note*]

**25** *Remarks:* The branch cuts are along the negative real axis.

```cpp
template<class T> complex<T> tan(const complex<T>& x);
```

**26** *Returns:* The complex tangent of `x`.

```cpp
template<class T> complex<T> tanh(const complex<T>& x);
```

**27** *Returns:* The complex hyperbolic tangent of `x`.
