# 17.2.1 Header `<cstddef>` synopsis [cstddef.syn]

[Contents](../README.md) · up: [17.2 Common definitions \[support.types\]](support.types.md) · prev: [17.2 Common definitions \[support.types\]](support.types.md) · next: [17.2.2 Header `<cstdlib>` synopsis \[cstdlib.syn\]](cstdlib.syn.md)

```cpp
namespace std {
  using ptrdiff_t = see below;
  using size_t = see below;
  using max_align_t = see below;
  using nullptr_t = decltype(nullptr);

  enum class byte : unsigned char {};

  // [support.types.byteops], byte type operations
  template<class IntType>
    constexpr byte& operator<<=(byte& b, IntType shift) noexcept;
  template<class IntType>
    constexpr byte operator<<(byte b, IntType shift) noexcept;
  template<class IntType>
    constexpr byte& operator>>=(byte& b, IntType shift) noexcept;
  template<class IntType>
    constexpr byte operator>>(byte b, IntType shift) noexcept;
  constexpr byte& operator|=(byte& l, byte r) noexcept;
  constexpr byte operator|(byte l, byte r) noexcept;
  constexpr byte& operator&=(byte& l, byte r) noexcept;
  constexpr byte operator&(byte l, byte r) noexcept;
  constexpr byte& operator^=(byte& l, byte r) noexcept;
  constexpr byte operator^(byte l, byte r) noexcept;
  constexpr byte operator~(byte b) noexcept;
  template<class IntType>
    constexpr IntType to_integer(byte b) noexcept;
}

#define NULL see below
#define offsetof(P, D) see below
```

**1** The contents and meaning of the header `<cstddef>` are the same as the C standard library header `<stddef.h>`, except that it does not declare the type `wchar_t`, that it also declares the type `byte` and its associated operations ([[support.types.byteops]](support.types.byteops.md)), and as noted in [[support.types.nullptr]](support.types.nullptr.md) and [[support.types.layout]](support.types.layout.md).

*See also:* ISO C 7.19
