# 28.4.1.1.1 `ctype` members [locale.ctype.members]

[Contents](../../README.md) · up: [28.4.1.1 Class template `ctype` \[locale.ctype\]](locale.ctype.md) · prev: [28.4.1.1 Class template `ctype` \[locale.ctype\]](locale.ctype.md) · next: [28.4.1.1.2 `ctype` virtual functions \[locale.ctype.virtuals\]](locale.ctype.virtuals.md)

```cpp
bool         is(mask m, charT c) const;
const charT* is(const charT* low, const charT* high, mask* vec) const;
```

**1** *Returns:* `do_is(m, c)` or `do_is(low, high, vec)`.

```cpp
const charT* scan_is(mask m, const charT* low, const charT* high) const;
```

**2** *Returns:* `do_scan_is(m, low, high)`.

```cpp
const charT* scan_not(mask m, const charT* low, const charT* high) const;
```

**3** *Returns:* `do_scan_not(m, low, high)`.

```cpp
charT        toupper(charT) const;
const charT* toupper(charT* low, const charT* high) const;
```

**4** *Returns:* `do_toupper(c)` or `do_toupper(low, high)`.

```cpp
charT        tolower(charT c) const;
const charT* tolower(charT* low, const charT* high) const;
```

**5** *Returns:* `do_tolower(c)` or `do_tolower(low, high)`.

```cpp
charT       widen(char c) const;
const char* widen(const char* low, const char* high, charT* to) const;
```

**6** *Returns:* `do_widen(c)` or `do_widen(low, high, to)`.

```cpp
char         narrow(charT c, char dfault) const;
const charT* narrow(const charT* low, const charT* high, char dfault, char* to) const;
```

**7** *Returns:* `do_narrow(c, dfault)` or `do_narrow(low, high, dfault, to)`.
