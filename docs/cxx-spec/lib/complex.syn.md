# 26.4.1 Header `<complex>` synopsis [complex.syn]

[Contents](../README.md) · up: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · prev: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · next: [26.4.2 Class template `complex` \[complex\]](complex.md)

```cpp
namespace std {
  // [complex], class template complex
  template<class T> class complex;

  // [complex.special], specializations
  template<> class complex<float>;
  template<> class complex<double>;
  template<> class complex<long double>;

  // [complex.ops], operators
  template<class T> constexpr complex<T> operator+(const complex<T>&, const complex<T>&);
  template<class T> constexpr complex<T> operator+(const complex<T>&, const T&);
  template<class T> constexpr complex<T> operator+(const T&, const complex<T>&);

  template<class T> constexpr complex<T> operator-(const complex<T>&, const complex<T>&);
  template<class T> constexpr complex<T> operator-(const complex<T>&, const T&);
  template<class T> constexpr complex<T> operator-(const T&, const complex<T>&);

  template<class T> constexpr complex<T> operator*(const complex<T>&, const complex<T>&);
  template<class T> constexpr complex<T> operator*(const complex<T>&, const T&);
  template<class T> constexpr complex<T> operator*(const T&, const complex<T>&);

  template<class T> constexpr complex<T> operator/(const complex<T>&, const complex<T>&);
  template<class T> constexpr complex<T> operator/(const complex<T>&, const T&);
  template<class T> constexpr complex<T> operator/(const T&, const complex<T>&);

  template<class T> constexpr complex<T> operator+(const complex<T>&);
  template<class T> constexpr complex<T> operator-(const complex<T>&);

  template<class T> constexpr bool operator==(const complex<T>&, const complex<T>&);
  template<class T> constexpr bool operator==(const complex<T>&, const T&);

  template<class T, class charT, class traits>
    basic_istream<charT, traits>& operator>>(basic_istream<charT, traits>&, complex<T>&);

  template<class T, class charT, class traits>
    basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>&, const complex<T>&);

  // [complex.value.ops], values
  template<class T> constexpr T real(const complex<T>&);
  template<class T> constexpr T imag(const complex<T>&);

  template<class T> T abs(const complex<T>&);
  template<class T> T arg(const complex<T>&);
  template<class T> constexpr T norm(const complex<T>&);

  template<class T> constexpr complex<T> conj(const complex<T>&);
  template<class T> complex<T> proj(const complex<T>&);
  template<class T> complex<T> polar(const T&, const T& = T());

  // [complex.transcendentals], transcendentals
  template<class T> complex<T> acos(const complex<T>&);
  template<class T> complex<T> asin(const complex<T>&);
  template<class T> complex<T> atan(const complex<T>&);

  template<class T> complex<T> acosh(const complex<T>&);
  template<class T> complex<T> asinh(const complex<T>&);
  template<class T> complex<T> atanh(const complex<T>&);

  template<class T> complex<T> cos  (const complex<T>&);
  template<class T> complex<T> cosh (const complex<T>&);
  template<class T> complex<T> exp  (const complex<T>&);
  template<class T> complex<T> log  (const complex<T>&);
  template<class T> complex<T> log10(const complex<T>&);

  template<class T> complex<T> pow  (const complex<T>&, const T&);
  template<class T> complex<T> pow  (const complex<T>&, const complex<T>&);
  template<class T> complex<T> pow  (const T&, const complex<T>&);

  template<class T> complex<T> sin  (const complex<T>&);
  template<class T> complex<T> sinh (const complex<T>&);
  template<class T> complex<T> sqrt (const complex<T>&);
  template<class T> complex<T> tan  (const complex<T>&);
  template<class T> complex<T> tanh (const complex<T>&);

  // [complex.literals], complex literals
  inline namespace literals {
  inline namespace complex_literals {
    constexpr complex<long double> operator""il(long double);
    constexpr complex<long double> operator""il(unsigned long long);
    constexpr complex<double> operator""i(long double);
    constexpr complex<double> operator""i(unsigned long long);
    constexpr complex<float> operator""if(long double);
    constexpr complex<float> operator""if(unsigned long long);
  }
  }
}
```
