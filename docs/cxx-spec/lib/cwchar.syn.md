# 21.5.4 Header `<cwchar>` synopsis [cwchar.syn]

[Contents](../README.md) · up: [21.5 Null-terminated sequence utilities \[c.strings\]](c.strings.md) · prev: [21.5.3 Header `<cstring>` synopsis \[cstring.syn\]](cstring.syn.md) · next: [21.5.5 Header `<cuchar>` synopsis \[cuchar.syn\]](cuchar.syn.md)

```cpp
namespace std {
  using size_t = see [support.types.layout];
  using mbstate_t = see below;
  using wint_t = see below;

  struct tm;

  int fwprintf(FILE* stream, const wchar_t* format, ...);
  int fwscanf(FILE* stream, const wchar_t* format, ...);
  int swprintf(wchar_t* s, size_t n, const wchar_t* format, ...);
  int swscanf(const wchar_t* s, const wchar_t* format, ...);
  int vfwprintf(FILE* stream, const wchar_t* format, va_list arg);
  int vfwscanf(FILE* stream, const wchar_t* format, va_list arg);
  int vswprintf(wchar_t* s, size_t n, const wchar_t* format, va_list arg);
  int vswscanf(const wchar_t* s, const wchar_t* format, va_list arg);
  int vwprintf(const wchar_t* format, va_list arg);
  int vwscanf(const wchar_t* format, va_list arg);
  int wprintf(const wchar_t* format, ...);
  int wscanf(const wchar_t* format, ...);
  wint_t fgetwc(FILE* stream);
  wchar_t* fgetws(wchar_t* s, int n, FILE* stream);
  wint_t fputwc(wchar_t c, FILE* stream);
  int fputws(const wchar_t* s, FILE* stream);
  int fwide(FILE* stream, int mode);
  wint_t getwc(FILE* stream);
  wint_t getwchar();
  wint_t putwc(wchar_t c, FILE* stream);
  wint_t putwchar(wchar_t c);
  wint_t ungetwc(wint_t c, FILE* stream);
  double wcstod(const wchar_t* nptr, wchar_t** endptr);
  float wcstof(const wchar_t* nptr, wchar_t** endptr);
  long double wcstold(const wchar_t* nptr, wchar_t** endptr);
  long int wcstol(const wchar_t* nptr, wchar_t** endptr, int base);
  long long int wcstoll(const wchar_t* nptr, wchar_t** endptr, int base);
  unsigned long int wcstoul(const wchar_t* nptr, wchar_t** endptr, int base);
  unsigned long long int wcstoull(const wchar_t* nptr, wchar_t** endptr, int base);
  wchar_t* wcscpy(wchar_t* s1, const wchar_t* s2);
  wchar_t* wcsncpy(wchar_t* s1, const wchar_t* s2, size_t n);
  wchar_t* wmemcpy(wchar_t* s1, const wchar_t* s2, size_t n);
  wchar_t* wmemmove(wchar_t* s1, const wchar_t* s2, size_t n);
  wchar_t* wcscat(wchar_t* s1, const wchar_t* s2);
  wchar_t* wcsncat(wchar_t* s1, const wchar_t* s2, size_t n);
  int wcscmp(const wchar_t* s1, const wchar_t* s2);
  int wcscoll(const wchar_t* s1, const wchar_t* s2);
  int wcsncmp(const wchar_t* s1, const wchar_t* s2, size_t n);
  size_t wcsxfrm(wchar_t* s1, const wchar_t* s2, size_t n);
  int wmemcmp(const wchar_t* s1, const wchar_t* s2, size_t n);
  const wchar_t* wcschr(const wchar_t* s, wchar_t c);           // see [library.c]
  wchar_t* wcschr(wchar_t* s, wchar_t c);                       // see [library.c]
  size_t wcscspn(const wchar_t* s1, const wchar_t* s2);
  const wchar_t* wcspbrk(const wchar_t* s1, const wchar_t* s2); // see [library.c]
  wchar_t* wcspbrk(wchar_t* s1, const wchar_t* s2);             // see [library.c]
  const wchar_t* wcsrchr(const wchar_t* s, wchar_t c);          // see [library.c]
  wchar_t* wcsrchr(wchar_t* s, wchar_t c);                      // see [library.c]
  size_t wcsspn(const wchar_t* s1, const wchar_t* s2);
  const wchar_t* wcsstr(const wchar_t* s1, const wchar_t* s2);  // see [library.c]
  wchar_t* wcsstr(wchar_t* s1, const wchar_t* s2);              // see [library.c]
  wchar_t* wcstok(wchar_t* s1, const wchar_t* s2, wchar_t** ptr);
  const wchar_t* wmemchr(const wchar_t* s, wchar_t c, size_t n);    // see [library.c]
  wchar_t* wmemchr(wchar_t* s, wchar_t c, size_t n);            // see [library.c]
  size_t wcslen(const wchar_t* s);
  wchar_t* wmemset(wchar_t* s, wchar_t c, size_t n);
  size_t wcsftime(wchar_t* s, size_t maxsize, const wchar_t* format, const struct tm* timeptr);
  wint_t btowc(int c);
  int wctob(wint_t c);

  // [c.mb.wcs], multibyte / wide string and character conversion functions
  int mbsinit(const mbstate_t* ps);
  size_t mbrlen(const char* s, size_t n, mbstate_t* ps);
  size_t mbrtowc(wchar_t* pwc, const char* s, size_t n, mbstate_t* ps);
  size_t wcrtomb(char* s, wchar_t wc, mbstate_t* ps);
  size_t mbsrtowcs(wchar_t* dst, const char** src, size_t len, mbstate_t* ps);
  size_t wcsrtombs(char* dst, const wchar_t** src, size_t len, mbstate_t* ps);
}

#define NULL see [support.types.nullptr]
#define WCHAR_MAX see below
#define WCHAR_MIN see below
#define WEOF see below
```

**1** The contents and meaning of the header `<cwchar>` are the same as the C standard library header `<wchar.h>`, except that it does not declare a type `wchar_t`.

**2** [*Note 1*: The functions `wcschr`, `wcspbrk`, `wcsrchr`, `wcsstr`, and `wmemchr` have different signatures in this document, but they have the same behavior as in the C standard library ([[library.c]](library.c.md)). — *end note*]

*See also:* ISO C 7.29
