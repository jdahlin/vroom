# 21.5.2 Header `<cwctype>` synopsis [cwctype.syn]

[Contents](../README.md) · up: [21.5 Null-terminated sequence utilities \[c.strings\]](c.strings.md) · prev: [21.5.1 Header `<cctype>` synopsis \[cctype.syn\]](cctype.syn.md) · next: [21.5.3 Header `<cstring>` synopsis \[cstring.syn\]](cstring.syn.md)

```cpp
namespace std {
  using wint_t = see below;
  using wctrans_t = see below;
  using wctype_t = see below;

  int iswalnum(wint_t wc);
  int iswalpha(wint_t wc);
  int iswblank(wint_t wc);
  int iswcntrl(wint_t wc);
  int iswdigit(wint_t wc);
  int iswgraph(wint_t wc);
  int iswlower(wint_t wc);
  int iswprint(wint_t wc);
  int iswpunct(wint_t wc);
  int iswspace(wint_t wc);
  int iswupper(wint_t wc);
  int iswxdigit(wint_t wc);
  int iswctype(wint_t wc, wctype_t desc);
  wctype_t wctype(const char* property);
  wint_t towlower(wint_t wc);
  wint_t towupper(wint_t wc);
  wint_t towctrans(wint_t wc, wctrans_t desc);
  wctrans_t wctrans(const char* property);
}

#define WEOF see below
```

**1** The contents and meaning of the header `<cwctype>` are the same as the C standard library header `<wctype.h>`.

*See also:* ISO C 7.30
