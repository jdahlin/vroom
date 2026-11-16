# 29.7.4.4 Standard `basic_istream` manipulators [istream.manip]

[Contents](../README.md) · up: [29.7.4 Input streams \[input.streams\]](input.streams.md) · prev: [29.7.4.3 Unformatted input functions \[istream.unformatted\]](istream.unformatted.md) · next: [29.7.4.5 Rvalue stream extraction \[istream.rvalue\]](istream.rvalue.md)

**1** Each instantiation of the function template specified in this subclause is a designated addressable function ([[namespace.std]](namespace.std.md)).

```cpp
template<class charT, class traits>
  basic_istream<charT, traits>& ws(basic_istream<charT, traits>& is);
```

**2** *Effects:* Behaves as an unformatted input function ([[istream.unformatted]](istream.unformatted.md)), except that it does not count the number of characters extracted and does not affect the value returned by subsequent calls to `is.gcount()`. After constructing a sentry object extracts characters as long as the next available character `c` is whitespace or until there are no more characters in the sequence. Whitespace characters are distinguished with the same criterion as used by `sentry::sentry` ([[istream.sentry]](istream.sentry.md)). If `ws` stops extracting characters because there are no more available it sets `eofbit`, but not `failbit`.

**3** *Returns:* `is`.
