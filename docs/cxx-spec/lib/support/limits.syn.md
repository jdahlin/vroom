# 17.3.3 Header `<limits>` synopsis [limits.syn]

[Contents](../../README.md) · up: [17.3 Implementation properties \[support.limits\]](support.limits.md) · prev: [17.3.2 Header `<version>` synopsis \[version.syn\]](version.syn.md) · next: [17.3.4 Floating-point type properties \[fp.style\]](fp.style.md)

```cpp
namespace std {
  // [fp.style], floating-point type properties
  enum float_round_style;
  enum float_denorm_style;

  // [numeric.limits], class template numeric_limits
  template<class T> class numeric_limits;

  template<class T> class numeric_limits<const T>;
  template<class T> class numeric_limits<volatile T>;
  template<class T> class numeric_limits<const volatile T>;

  template<> class numeric_limits<bool>;

  template<> class numeric_limits<char>;
  template<> class numeric_limits<signed char>;
  template<> class numeric_limits<unsigned char>;
  template<> class numeric_limits<char8_t>;
  template<> class numeric_limits<char16_t>;
  template<> class numeric_limits<char32_t>;
  template<> class numeric_limits<wchar_t>;

  template<> class numeric_limits<short>;
  template<> class numeric_limits<int>;
  template<> class numeric_limits<long>;
  template<> class numeric_limits<long long>;
  template<> class numeric_limits<unsigned short>;
  template<> class numeric_limits<unsigned int>;
  template<> class numeric_limits<unsigned long>;
  template<> class numeric_limits<unsigned long long>;

  template<> class numeric_limits<float>;
  template<> class numeric_limits<double>;
  template<> class numeric_limits<long double>;
}
```
