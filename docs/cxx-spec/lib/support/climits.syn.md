# 17.3.6 Header `<climits>` synopsis [climits.syn]

[Contents](../../README.md) · up: [17.3 Implementation properties \[support.limits\]](support.limits.md) · prev: [17.3.5.2 `numeric_limits` specializations \[numeric.special\]](numeric.special.md) · next: [17.3.7 Header `<cfloat>` synopsis \[cfloat.syn\]](cfloat.syn.md)

```cpp
#define CHAR_BIT see below
#define SCHAR_MIN see below
#define SCHAR_MAX see below
#define UCHAR_MAX see below
#define CHAR_MIN see below
#define CHAR_MAX see below
#define MB_LEN_MAX see below
#define SHRT_MIN see below
#define SHRT_MAX see below
#define USHRT_MAX see below
#define INT_MIN see below
#define INT_MAX see below
#define UINT_MAX see below
#define LONG_MIN see below
#define LONG_MAX see below
#define ULONG_MAX see below
#define LLONG_MIN see below
#define LLONG_MAX see below
#define ULLONG_MAX see below
```

**1** The header `<climits>` defines all macros the same as the C standard library header `<limits.h>`. [*Note 1*: The types of the constants defined by macros in `<climits>` are not required to match the types to which the macros refer. — *end note*]

*See also:* ISO C 5.2.4.2.1
