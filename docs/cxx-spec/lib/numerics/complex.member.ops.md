# 26.4.5 Member operators [complex.member.ops]

[Contents](../../README.md) · up: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · prev: [26.4.4 Member functions \[complex.members\]](complex.members.md) · next: [26.4.6 Non-member operations \[complex.ops\]](complex.ops.md)

```cpp
constexpr complex& operator+=(const T& rhs);
```

**1** *Effects:* Adds the scalar value `rhs` to the real part of the complex value `*this` and stores the result in the real part of `*this`, leaving the imaginary part unchanged.

**2** *Returns:* `*this`.

```cpp
constexpr complex& operator-=(const T& rhs);
```

**3** *Effects:* Subtracts the scalar value `rhs` from the real part of the complex value `*this` and stores the result in the real part of `*this`, leaving the imaginary part unchanged.

**4** *Returns:* `*this`.

```cpp
constexpr complex& operator*=(const T& rhs);
```

**5** *Effects:* Multiplies the scalar value `rhs` by the complex value `*this` and stores the result in `*this`.

**6** *Returns:* `*this`.

```cpp
constexpr complex& operator/=(const T& rhs);
```

**7** *Effects:* Divides the scalar value `rhs` into the complex value `*this` and stores the result in `*this`.

**8** *Returns:* `*this`.

```cpp
template<class X> constexpr complex& operator+=(const complex<X>& rhs);
```

**9** *Effects:* Adds the complex value `rhs` to the complex value `*this` and stores the sum in `*this`.

**10** *Returns:* `*this`.

```cpp
template<class X> constexpr complex& operator-=(const complex<X>& rhs);
```

**11** *Effects:* Subtracts the complex value `rhs` from the complex value `*this` and stores the difference in `*this`.

**12** *Returns:* `*this`.

```cpp
template<class X> constexpr complex& operator*=(const complex<X>& rhs);
```

**13** *Effects:* Multiplies the complex value `rhs` by the complex value `*this` and stores the product in `*this`.

**14** *Returns:* `*this`.

```cpp
template<class X> constexpr complex& operator/=(const complex<X>& rhs);
```

**15** *Effects:* Divides the complex value `rhs` into the complex value `*this` and stores the quotient in `*this`.

**16** *Returns:* `*this`.
