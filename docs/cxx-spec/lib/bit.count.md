# 26.5.6 Counting [bit.count]

[Contents](../README.md) · up: [26.5 Bit manipulation \[bit\]](bit.md) · prev: [26.5.5 Rotating \[bit.rotate\]](bit.rotate.md) · next: [26.5.7 Endian \[bit.endian\]](bit.endian.md)

In the following descriptions, let `N` denote `numeric_limits<T>::digits`.

```cpp
template<class T>
  constexpr int countl_zero(T x) noexcept;
```

**1** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)).

**2** *Returns:* The number of consecutive `0` bits in the value of `x`, starting from the most significant bit. [*Note 1*: Returns `N` if `x == 0`. — *end note*]

```cpp
template<class T>
  constexpr int countl_one(T x) noexcept;
```

**3** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)).

**4** *Returns:* The number of consecutive `1` bits in the value of `x`, starting from the most significant bit. [*Note 2*: Returns `N` if `x == numeric_limits<T>::max()`. — *end note*]

```cpp
template<class T>
  constexpr int countr_zero(T x) noexcept;
```

**5** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)).

**6** *Returns:* The number of consecutive `0` bits in the value of `x`, starting from the least significant bit. [*Note 3*: Returns `N` if `x == 0`. — *end note*]

```cpp
template<class T>
  constexpr int countr_one(T x) noexcept;
```

**7** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)).

**8** *Returns:* The number of consecutive `1` bits in the value of `x`, starting from the least significant bit. [*Note 4*: Returns `N` if `x == numeric_limits<T>::max()`. — *end note*]

```cpp
template<class T>
  constexpr int popcount(T x) noexcept;
```

**9** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)).

**10** *Returns:* The number of `1` bits in the value of `x`.
