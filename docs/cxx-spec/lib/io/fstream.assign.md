# 29.9.5.2 Assignment and swap [fstream.assign]

[Contents](../../README.md) · up: [29.9.5 Class template `basic_fstream` \[fstream\]](fstream.md) · prev: [29.9.5.1 Constructors \[fstream.cons\]](fstream.cons.md) · next: [29.9.5.3 Member functions \[fstream.members\]](fstream.members.md)

```cpp
void swap(basic_fstream& rhs);
```

**1** *Effects:* Exchanges the state of `*this` and `rhs` by calling `basic_iostream<charT,traits>::swap(rhs)` and `sb.swap(rhs.sb)`.

```cpp
template<class charT, class traits>
  void swap(basic_fstream<charT, traits>& x,
            basic_fstream<charT, traits>& y);
```

**2** *Effects:* Equivalent to: `x.swap(y)`.
