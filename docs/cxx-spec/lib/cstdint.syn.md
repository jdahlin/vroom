# 17.4.1 Header `<cstdint>` synopsis [cstdint.syn]

[Contents](../README.md) · up: [17.4 Integer types \[cstdint\]](cstdint.md) · prev: [17.4 Integer types \[cstdint\]](cstdint.md) · next: [17.5 Startup and termination \[support.start.term\]](support.start.term.md)

```cpp
namespace std {
  using int8_t         = signed integer type;  // optional
  using int16_t        = signed integer type;  // optional
  using int32_t        = signed integer type;  // optional
  using int64_t        = signed integer type;  // optional

  using int_fast8_t    = signed integer type;
  using int_fast16_t   = signed integer type;
  using int_fast32_t   = signed integer type;
  using int_fast64_t   = signed integer type;

  using int_least8_t   = signed integer type;
  using int_least16_t  = signed integer type;
  using int_least32_t  = signed integer type;
  using int_least64_t  = signed integer type;

  using intmax_t       = signed integer type;
  using intptr_t       = signed integer type;   // optional

  using uint8_t        = unsigned integer type; // optional
  using uint16_t       = unsigned integer type; // optional
  using uint32_t       = unsigned integer type; // optional
  using uint64_t       = unsigned integer type; // optional

  using uint_fast8_t   = unsigned integer type;
  using uint_fast16_t  = unsigned integer type;
  using uint_fast32_t  = unsigned integer type;
  using uint_fast64_t  = unsigned integer type;

  using uint_least8_t  = unsigned integer type;
  using uint_least16_t = unsigned integer type;
  using uint_least32_t = unsigned integer type;
  using uint_least64_t = unsigned integer type;

  using uintmax_t      = unsigned integer type;
  using uintptr_t      = unsigned integer type; // optional
}
```

**1** The header also defines numerous macros of the form:

```cpp
INT_[FAST LEAST]{8 16 32 64}_MIN
[U]INT_[FAST LEAST]{8 16 32 64}_MAX
INT{MAX PTR}_MIN
[U]INT{MAX PTR}_MAX
{PTRDIFF SIG_ATOMIC WCHAR WINT}{_MAX _MIN}
SIZE_MAX
```

plus function macros of the form:

```cpp
[U]INT{8 16 32 64 MAX}_C
```

**2** The header defines all types and macros the same as the C standard library header `<stdint.h>`.

*See also:* ISO C 7.20
