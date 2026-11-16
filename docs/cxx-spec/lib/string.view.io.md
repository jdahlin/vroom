# 21.4.5 Inserters and extractors [string.view.io]

[Contents](../README.md) · up: [21.4 String view classes \[string.view\]](string.view.md) · prev: [21.4.4 Non-member comparison functions \[string.view.comparison\]](string.view.comparison.md) · next: [21.4.6 Hash support \[string.view.hash\]](string.view.hash.md)

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, basic_string_view<charT, traits> str);
```

**1** *Effects:* Behaves as a formatted output function ([[ostream.formatted.reqmts]](ostream.formatted.reqmts.md)) of `os`. Forms a character sequence `seq`, initially consisting of the elements defined by the range `[str.begin(), str.end())`. Determines padding for `seq` as described in [[ostream.formatted.reqmts]](ostream.formatted.reqmts.md). Then inserts `seq` as if by calling `os.rdbuf()->sputn(seq, n)`, where `n` is the larger of `os.width()` and `str.size()`; then calls `os.width(0)`.

**2** *Returns:* `os`
