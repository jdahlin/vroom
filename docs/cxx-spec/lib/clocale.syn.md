# 28.5.1 Header `<clocale>` synopsis [clocale.syn]

[Contents](../README.md) · up: [28.5 C library locales \[c.locales\]](c.locales.md) · prev: [28.5 C library locales \[c.locales\]](c.locales.md) · next: [29 Input/output library \[input.output\]](input.output.md)

```cpp
namespace std {
  struct lconv;

  char* setlocale(int category, const char* locale);
  lconv* localeconv();
}

#define NULL see [support.types.nullptr]
#define LC_ALL see below
#define LC_COLLATE see below
#define LC_CTYPE see below
#define LC_MONETARY see below
#define LC_NUMERIC see below
#define LC_TIME see below
```

**1** The contents and meaning of the header `<clocale>` are the same as the C standard library header `<locale.h>`.

**2** Calls to the function `setlocale` may introduce a data race ([[res.on.data.races]](res.on.data.races.md)) with other calls to `setlocale` or with calls to the functions listed in [Table 114](clocale.syn.md#tab:setlocale.data.races).

*See also:* ISO C 7.11

<a id="tab:setlocale.data.races"></a>
**Table 114: Potential `setlocale` data races** [tab:setlocale.data.races]

|  |  |  |  |  |
| --- | --- | --- | --- | --- |
| `fprintf` | `isprint` | `iswdigit` | `localeconv` | `tolower` |
| `fscanf` | `ispunct` | `iswgraph` | `mblen` | `toupper` |
| `isalnum` | `isspace` | `iswlower` | `mbstowcs` | `towlower` |
| `isalpha` | `isupper` | `iswprint` | `mbtowc` | `towupper` |
| `isblank` | `iswalnum` | `iswpunct` | `setlocale` | `wcscoll` |
| `iscntrl` | `iswalpha` | `iswspace` | `strcoll` | `wcstod` |
| `isdigit` | `iswblank` | `iswupper` | `strerror` | `wcstombs` |
| `isgraph` | `iswcntrl` | `iswxdigit` | `strtod` | `wcsxfrm` |
| `islower` | `iswctype` | `isxdigit` | `strxfrm` | `wctomb` |
