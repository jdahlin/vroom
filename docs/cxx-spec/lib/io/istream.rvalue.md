# 29.7.4.5 Rvalue stream extraction [istream.rvalue]

[Contents](../../README.md) · up: [29.7.4 Input streams \[input.streams\]](input.streams.md) · prev: [29.7.4.4 Standard `basic_istream` manipulators \[istream.manip\]](istream.manip.md) · next: [29.7.4.6 Class template `basic_iostream` \[iostreamclass\]](iostreamclass.md)

```cpp
template<class Istream, class T>
  Istream&& operator>>(Istream&& is, T&& x);
```

**1** *Constraints:* The expression `is >> std::forward<T>(x)` is well-formed when treated as an unevaluated operand and `Istream` is publicly and unambiguously derived from `ios_base`.

**2** *Effects:* Equivalent to:

```cpp
is >> std::forward<T>(x);
return std::move(is);
```
