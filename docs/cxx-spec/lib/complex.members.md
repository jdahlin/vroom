# 26.4.4 Member functions [complex.members]

[Contents](../README.md) · up: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · prev: [26.4.3 Specializations \[complex.special\]](complex.special.md) · next: [26.4.5 Member operators \[complex.member.ops\]](complex.member.ops.md)

```cpp
template<class T> constexpr complex(const T& re = T(), const T& im = T());
```

**1** *Postconditions:* `real() == re && imag() == im` is `true`.

```cpp
constexpr T real() const;
```

**2** *Returns:* The value of the real component.

```cpp
constexpr void real(T val);
```

**3** *Effects:* Assigns `val` to the real component.

```cpp
constexpr T imag() const;
```

**4** *Returns:* The value of the imaginary component.

```cpp
constexpr void imag(T val);
```

**5** *Effects:* Assigns `val` to the imaginary component.
