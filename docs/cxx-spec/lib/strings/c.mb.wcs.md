# 21.5.6 Multibyte / wide string and character conversion functions [c.mb.wcs]

[Contents](../../README.md) · up: [21.5 Null-terminated sequence utilities \[c.strings\]](c.strings.md) · prev: [21.5.5 Header `<cuchar>` synopsis \[cuchar.syn\]](cuchar.syn.md) · next: [22 Containers library \[containers\]](../containers/containers.md)

**1** [*Note 1*: The headers `<cstdlib>` ([[cstdlib.syn]](../support/cstdlib.syn.md)), `<cuchar>` ([[cuchar.syn]](cuchar.syn.md)), and `<cwchar>` ([[cwchar.syn]](cwchar.syn.md)) declare the functions described in this subclause. — *end note*]

```cpp
int mbsinit(const mbstate_t* ps);
int mblen(const char* s, size_t n);
size_t mbstowcs(wchar_t* pwcs, const char* s, size_t n);
size_t wcstombs(char* s, const wchar_t* pwcs, size_t n);
```

**2** *Effects:* These functions have the semantics specified in the C standard library.

*See also:* ISO C 7.22.7.1, 7.22.8, 7.29.6.2.1

```cpp
int mbtowc(wchar_t* pwc, const char* s, size_t n);
int wctomb(char* s, wchar_t wchar);
```

**3** *Effects:* These functions have the semantics specified in the C standard library.

**4** *Remarks:* Calls to these functions may introduce a data race ([[res.on.data.races]](../library/res.on.data.races.md)) with other calls to the same function.

*See also:* ISO C 7.22.7

```cpp
size_t mbrlen(const char* s, size_t n, mbstate_t* ps);
size_t mbrtowc(wchar_t* pwc, const char* s, size_t n, mbstate_t* ps);
size_t wcrtomb(char* s, wchar_t wc, mbstate_t* ps);
size_t mbsrtowcs(wchar_t* dst, const char** src, size_t len, mbstate_t* ps);
size_t wcsrtombs(char* dst, const wchar_t** src, size_t len, mbstate_t* ps);
```

**5** *Effects:* These functions have the semantics specified in the C standard library.

**6** *Remarks:* Calling these functions with an `mbstate_t*` argument that is a null pointer value may introduce a data race ([[res.on.data.races]](../library/res.on.data.races.md)) with other calls to the same function with an `mbstate_t*` argument that is a null pointer value.

*See also:* ISO C 7.29.6.3

```cpp
size_t mbrtoc8(char8_t* pc8, const char* s, size_t n, mbstate_t* ps);
```

**7** *Effects:* If `s` is a null pointer, equivalent to `mbrtoc8(nullptr, "", 1, ps)`. Otherwise, the function inspects at most `n` bytes beginning with the byte pointed to by `s` to determine the number of bytes needed to complete the next multibyte character (including any shift sequences). If the function determines that the next multibyte character is complete and valid, it determines the values of the corresponding UTF-8 code units and then, if `pc8` is not a null pointer, stores the value of the first (or only) such code unit in the object pointed to by `pc8`. Subsequent calls will store successive UTF-8 code units without consuming any additional input until all the code units have been stored. If the corresponding Unicode character is U+0000, the resulting state described is the initial conversion state.

**8** *Returns:* The first of the following that applies (given the current conversion state):

- `0`, if the next `n` or fewer bytes complete the multibyte character that corresponds to the U+0000 Unicode character (which is the value stored).
- between `1` and `n` (inclusive), if the next n or fewer bytes complete a valid multibyte character (which is the value stored); the value returned is the number of bytes that complete the multibyte character.
- `(size_t)(-3)`, if the next character resulting from a previous call has been stored (no bytes from the input have been consumed by this call).
- `(size_t)(-2)`, if the next `n` bytes contribute to an incomplete (but potentially valid) multibyte character, and all `n` bytes have been processed (no value is stored).
- `(size_t)(-1)`, if an encoding error occurs, in which case the next `n` or fewer bytes do not contribute to a complete and valid multibyte character (no value is stored); the value of the macro `EILSEQ` is stored in `errno`, and the conversion state is unspecified.

```cpp
size_t c8rtomb(char* s, char8_t c8, mbstate_t* ps);
```

**9** *Effects:* If `s` is a null pointer, equivalent to `c8rtomb(buf, u8'\0', ps)` where `buf` is an internal buffer. Otherwise, if `c8` completes a sequence of valid UTF-8 code units, determines the number of bytes needed to represent the multibyte character (including any shift sequences), and stores the multibyte character representation in the array whose first element is pointed to by `s`. At most `MB_CUR_MAX` bytes are stored. If the multibyte character is a null character, a null byte is stored, preceded by any shift sequence needed to restore the initial shift state; the resulting state described is the initial conversion state.

**10** *Returns:* The number of bytes stored in the array object (including any shift sequences). If `c8` does not contribute to a sequence of `char8_t` corresponding to a valid multibyte character, the value of the macro `EILSEQ` is stored in `errno`, `(size_t) (-1)` is returned, and the conversion state is unspecified.

**11** *Remarks:* Calls to `c8rtomb` with a null pointer argument for `s` may introduce a data race ([[res.on.data.races]](../library/res.on.data.races.md)) with other calls to `c8rtomb` with a null pointer argument for `s`.
