# 28.4.1.3.2 Members [facet.ctype.char.members]

[Contents](../README.md) · up: [28.4.1.3 `ctype<char>` specialization \[facet.ctype.special\]](facet.ctype.special.md) · prev: [28.4.1.3.1 Destructor \[facet.ctype.char.dtor\]](facet.ctype.char.dtor.md) · next: [28.4.1.3.3 Static members \[facet.ctype.char.statics\]](facet.ctype.char.statics.md)

**1** In the following member descriptions, for `unsigned char` values `v` where `v >= table_size`, `table()[v]` is assumed to have an implementation-specific value (possibly different for each such value `v`) without performing the array lookup.

```cpp
explicit ctype(const mask* tbl = nullptr, bool del = false, size_t refs = 0);
```

**2** *Preconditions:* Either `tbl == nullptr` is `true` or `[tbl, tbl+table_size)` is a valid range.

**3** *Effects:* Passes its `refs` argument to its base class constructor.

```cpp
bool        is(mask m, char c) const;
const char* is(const char* low, const char* high, mask* vec) const;
```

**4** *Effects:* The second form, for all `*p` in the range `[low, high)`, assigns into `vec[p - low]` the value `table()[(unsigned char)*p]`.

**5** *Returns:* The first form returns `table()[(unsigned char)c] & m`; the second form returns `high`.

```cpp
const char* scan_is(mask m, const char* low, const char* high) const;
```

**6** *Returns:* The smallest `p` in the range `[low, high)` such that

```cpp
table()[(unsigned char) *p] & m
```

is `true`.

```cpp
const char* scan_not(mask m, const char* low, const char* high) const;
```

**7** *Returns:* The smallest `p` in the range `[low, high)` such that

```cpp
table()[(unsigned char) *p] & m
```

is `false`.

```cpp
char        toupper(char c) const;
const char* toupper(char* low, const char* high) const;
```

**8** *Returns:* `do_toupper(c)` or `do_toupper(low, high)`, respectively.

```cpp
char        tolower(char c) const;
const char* tolower(char* low, const char* high) const;
```

**9** *Returns:* `do_tolower(c)` or `do_tolower(low, high)`, respectively.

```cpp
char  widen(char c) const;
const char* widen(const char* low, const char* high, char* to) const;
```

**10** *Returns:* `do_widen(c)` or `do_widen(low, high, to)`, respectively.

```cpp
char        narrow(char c, char dfault) const;
const char* narrow(const char* low, const char* high, char dfault, char* to) const;
```

**11** *Returns:* `do_narrow(c, dfault)` or `do_narrow(low, high, dfault, to)`, respectively.

```cpp
const mask* table() const noexcept;
```

**12** *Returns:* The first constructor argument, if it was nonzero, otherwise `classic_table()`.
