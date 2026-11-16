# 17.2.5 `byte` type operations [support.types.byteops]

[Contents](../../README.md) · up: [17.2 Common definitions \[support.types\]](support.types.md) · prev: [17.2.4 Sizes, alignments, and offsets \[support.types.layout\]](support.types.layout.md) · next: [17.3 Implementation properties \[support.limits\]](support.limits.md)

```cpp
template<class IntType>
  constexpr byte& operator<<=(byte& b, IntType shift) noexcept;
```

**1** *Constraints:* `is_integral_v<IntType>` is `true`.

**2** *Effects:* Equivalent to: `return b = b << shift;`

```cpp
template<class IntType>
  constexpr byte operator<<(byte b, IntType shift) noexcept;
```

**3** *Constraints:* `is_integral_v<IntType>` is `true`.

**4** *Effects:* Equivalent to:

```cpp
return static_cast<byte>(static_cast<unsigned int>(b) << shift);
```

```cpp
template<class IntType>
  constexpr byte& operator>>=(byte& b, IntType shift) noexcept;
```

**5** *Constraints:* `is_integral_v<IntType>` is `true`.

**6** *Effects:* Equivalent to: `return b = b >> shift;`

```cpp
template<class IntType>
  constexpr byte operator>>(byte b, IntType shift) noexcept;
```

**7** *Constraints:* `is_integral_v<IntType>` is `true`.

**8** *Effects:* Equivalent to:

```cpp
return static_cast<byte>(static_cast<unsigned int>(b) >> shift);
```

```cpp
constexpr byte& operator|=(byte& l, byte r) noexcept;
```

**9** *Effects:* Equivalent to: `return l = l | r;`

```cpp
constexpr byte operator|(byte l, byte r) noexcept;
```

**10** *Effects:* Equivalent to:

```cpp
return static_cast<byte>(static_cast<unsigned int>(l) | static_cast<unsigned int>(r));
```

```cpp
constexpr byte& operator&=(byte& l, byte r) noexcept;
```

**11** *Effects:* Equivalent to: `return l = l & r;`

```cpp
constexpr byte operator&(byte l, byte r) noexcept;
```

**12** *Effects:* Equivalent to:

```cpp
return static_cast<byte>(static_cast<unsigned int>(l) & static_cast<unsigned int>(r));
```

```cpp
constexpr byte& operator^=(byte& l, byte r) noexcept;
```

**13** *Effects:* Equivalent to: `return l = l ^ r;`

```cpp
constexpr byte operator^(byte l, byte r) noexcept;
```

**14** *Effects:* Equivalent to:

```cpp
return static_cast<byte>(static_cast<unsigned int>(l) ^ static_cast<unsigned int>(r));
```

```cpp
constexpr byte operator~(byte b) noexcept;
```

**15** *Effects:* Equivalent to:

```cpp
return static_cast<byte>(~static_cast<unsigned int>(b));
```

```cpp
template<class IntType>
  constexpr IntType to_integer(byte b) noexcept;
```

**16** *Constraints:* `is_integral_v<IntType>` is `true`.

**17** *Effects:* Equivalent to: `return static_cast<IntType>(b);`
