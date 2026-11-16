# 26.4.2 Class template `complex` [complex]

[Contents](../../README.md) · up: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · prev: [26.4.1 Header `<complex>` synopsis \[complex.syn\]](complex.syn.md) · next: [26.4.3 Specializations \[complex.special\]](complex.special.md)

```cpp
namespace std {
  template<class T> class complex {
  public:
    using value_type = T;

    constexpr complex(const T& re = T(), const T& im = T());
    constexpr complex(const complex&);
    template<class X> constexpr complex(const complex<X>&);

    constexpr T real() const;
    constexpr void real(T);
    constexpr T imag() const;
    constexpr void imag(T);

    constexpr complex& operator= (const T&);
    constexpr complex& operator+=(const T&);
    constexpr complex& operator-=(const T&);
    constexpr complex& operator*=(const T&);
    constexpr complex& operator/=(const T&);

    constexpr complex& operator=(const complex&);
    template<class X> constexpr complex& operator= (const complex<X>&);
    template<class X> constexpr complex& operator+=(const complex<X>&);
    template<class X> constexpr complex& operator-=(const complex<X>&);
    template<class X> constexpr complex& operator*=(const complex<X>&);
    template<class X> constexpr complex& operator/=(const complex<X>&);
  };
}
```

**1** The class `complex` describes an object that can store the Cartesian components, `real()` and `imag()`, of a complex number.
