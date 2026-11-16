# 29.7.5.5 Rvalue stream insertion [ostream.rvalue]

[Contents](../README.md) · up: [29.7.5 Output streams \[output.streams\]](output.streams.md) · prev: [29.7.5.4 Standard manipulators \[ostream.manip\]](ostream.manip.md) · next: [29.7.6 Standard manipulators \[std.manip\]](std.manip.md)

```cpp
template<class Ostream, class T>
  Ostream&& operator<<(Ostream&& os, const T& x);
```

**1** *Constraints:* The expression `os << x` is well-formed when treated as an unevaluated operand and `Ostream` is publicly and unambiguously derived from `ios_base`.

**2** *Effects:* As if by: `os << x;`

**3** *Returns:* `std::move(os)`.
