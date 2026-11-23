# 21.3.3.4 Inserters and extractors [string.io]

[Contents](../../README.md) · up: [21.3.3 Non-member functions \[string.nonmembers\]](string.nonmembers.md) · prev: [21.3.3.3 `swap` \[string.special\]](string.special.md) · next: [21.3.3.5 Erasure \[string.erasure\]](string.erasure.md)

```cpp
template<class charT, class traits, class Allocator>
  basic_istream<charT, traits>&
    operator>>(basic_istream<charT, traits>& is, basic_string<charT, traits, Allocator>& str);
```

**1** *Effects:* Behaves as a formatted input function ([[istream.formatted.reqmts]](../io/istream.formatted.reqmts.md)). After constructing a `sentry` object, if the sentry converts to `true`, calls `str.erase()` and then extracts characters from `is` and appends them to `str` as if by calling `str.append(1, c)`. If `is.width()` is greater than zero, the maximum number `n` of characters appended is `is.width()`; otherwise `n` is `str.max_size()`. Characters are extracted and appended until any of the following occurs:

- *n* characters are stored;
- end-of-file occurs on the input sequence;
- `isspace(c, is.getloc())` is `true` for the next available input character *c*.

**2** After the last character (if any) is extracted, `is.width(0)` is called and the `sentry` object is destroyed.

**3** If the function extracts no characters, it calls `is.setstate(ios_base::failbit)`, which may throw `ios_base::failure` ([[iostate.flags]](../io/iostate.flags.md)).

**4** *Returns:* `is`.

```cpp
template<class charT, class traits, class Allocator>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os,
               const basic_string<charT, traits, Allocator>& str);
```

**5** *Effects:* Equivalent to: `return os << basic_string_view<charT, traits>(str);`

```cpp
template<class charT, class traits, class Allocator>
  basic_istream<charT, traits>&
    getline(basic_istream<charT, traits>& is,
            basic_string<charT, traits, Allocator>& str,
            charT delim);
template<class charT, class traits, class Allocator>
  basic_istream<charT, traits>&
    getline(basic_istream<charT, traits>&& is,
            basic_string<charT, traits, Allocator>& str,
            charT delim);
```

**6** *Effects:* Behaves as an unformatted input function ([[istream.unformatted]](../io/istream.unformatted.md)), except that it does not affect the value returned by subsequent calls to `basic_istream<>::gcount()`. After constructing a `sentry` object, if the sentry converts to `true`, calls `str.erase()` and then extracts characters from `is` and appends them to `str` as if by calling `str.append(1, c)` until any of the following occurs:

- end-of-file occurs on the input sequence (in which case, the `getline` function calls `is.setstate(ios_base::eofbit)`).
- `traits::eq(c, delim)` for the next available input character *c* (in which case, *c* is extracted but not appended) ([[iostate.flags]](../io/iostate.flags.md))
- `str.max_size()` characters are stored (in which case, the function calls `is.setstate(ios_base::failbit))` ([[iostate.flags]](../io/iostate.flags.md))

**7** The conditions are tested in the order shown. In any case, after the last character is extracted, the `sentry` object is destroyed.

**8** If the function extracts no characters, it calls `is.setstate(ios_base::failbit)` which may throw `ios_base::failure` ([[iostate.flags]](../io/iostate.flags.md)).

**9** *Returns:* `is`.

```cpp
template<class charT, class traits, class Allocator>
  basic_istream<charT, traits>&
    getline(basic_istream<charT, traits>& is,
            basic_string<charT, traits, Allocator>& str);
template<class charT, class traits, class Allocator>
  basic_istream<charT, traits>&
    getline(basic_istream<charT, traits>&& is,
            basic_string<charT, traits, Allocator>& str);
```

**10** *Returns:* `getline(is, str, is.widen('\n'))`.
