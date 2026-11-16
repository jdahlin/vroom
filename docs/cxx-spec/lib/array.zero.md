# 22.3.7.5 Zero-sized arrays [array.zero]

[Contents](../README.md) · up: [22.3.7 Class template `array` \[array\]](array.md) · prev: [22.3.7.4 Specialized algorithms \[array.special\]](array.special.md) · next: [22.3.7.6 Array creation functions \[array.creation\]](array.creation.md)

**1** `array` shall provide support for the special case `N == 0`.

**2** In the case that `N == 0`, `begin() == end() ==` unique value. The return value of `data()` is unspecified.

**3** The effect of calling `front()` or `back()` for a zero-sized array is undefined.

**4** Member function `swap()` shall have a non-throwing exception specification.
