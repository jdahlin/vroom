# 26.4.6 Non-member operations [complex.ops]

[Contents](../README.md) · up: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · prev: [26.4.5 Member operators \[complex.member.ops\]](complex.member.ops.md) · next: [26.4.7 Value operations \[complex.value.ops\]](complex.value.ops.md)

```cpp
template<class T> constexpr complex<T> operator+(const complex<T>& lhs);
```

**1** *Returns:* `complex<T>(lhs)`.

```cpp
template<class T> constexpr complex<T> operator+(const complex<T>& lhs, const complex<T>& rhs);
template<class T> constexpr complex<T> operator+(const complex<T>& lhs, const T& rhs);
template<class T> constexpr complex<T> operator+(const T& lhs, const complex<T>& rhs);
```

**2** *Returns:* `complex<T>(lhs) += rhs`.

```cpp
template<class T> constexpr complex<T> operator-(const complex<T>& lhs);
```

**3** *Returns:* `complex<T>(-lhs.real(),-lhs.imag())`.

```cpp
template<class T> constexpr complex<T> operator-(const complex<T>& lhs, const complex<T>& rhs);
template<class T> constexpr complex<T> operator-(const complex<T>& lhs, const T& rhs);
template<class T> constexpr complex<T> operator-(const T& lhs, const complex<T>& rhs);
```

**4** *Returns:* `complex<T>(lhs) -= rhs`.

```cpp
template<class T> constexpr complex<T> operator*(const complex<T>& lhs, const complex<T>& rhs);
template<class T> constexpr complex<T> operator*(const complex<T>& lhs, const T& rhs);
template<class T> constexpr complex<T> operator*(const T& lhs, const complex<T>& rhs);
```

**5** *Returns:* `complex<T>(lhs) *= rhs`.

```cpp
template<class T> constexpr complex<T> operator/(const complex<T>& lhs, const complex<T>& rhs);
template<class T> constexpr complex<T> operator/(const complex<T>& lhs, const T& rhs);
template<class T> constexpr complex<T> operator/(const T& lhs, const complex<T>& rhs);
```

**6** *Returns:* `complex<T>(lhs) /= rhs`.

```cpp
template<class T> constexpr bool operator==(const complex<T>& lhs, const complex<T>& rhs);
template<class T> constexpr bool operator==(const complex<T>& lhs, const T& rhs);
```

**7** *Returns:* `lhs.real() == rhs.real() && lhs.imag() == rhs.imag()`.

**8** *Remarks:* The imaginary part is assumed to be `T()`, or 0.0, for the `T` arguments.

```cpp
template<class T, class charT, class traits>
  basic_istream<charT, traits>& operator>>(basic_istream<charT, traits>& is, complex<T>& x);
```

**9** *Preconditions:* The input values are convertible to `T`.

**10** *Effects:* Extracts a complex number `x` of the form: `u`, `(u)`, or `(u,v)`, where `u` is the real part and `v` is the imaginary part ([[istream.formatted]](istream.formatted.md)).

**11** If bad input is encountered, calls `is.setstate(ios_base::failbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))).

**12** *Returns:* `is`.

**13** *Remarks:* This extraction is performed as a series of simpler extractions. Therefore, the skipping of whitespace is specified to be the same for each of the simpler extractions.

```cpp
template<class T, class charT, class traits>
  basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>& o, const complex<T>& x);
```

**14** *Effects:* Inserts the complex number `x` onto the stream `o` as if it were implemented as follows:

```cpp
basic_ostringstream<charT, traits> s;
s.flags(o.flags());
s.imbue(o.getloc());
s.precision(o.precision());
s << '(' << x.real() << "," << x.imag() << ')';
return o << s.str();
```

**15** [*Note 1*: In a locale in which comma is used as a decimal point character, the use of comma as a field separator can be ambiguous. Inserting `showpoint` into the output stream forces all outputs to show an explicit decimal point character; as a result, all inserted sequences of complex numbers can be extracted unambiguously. — *end note*]
