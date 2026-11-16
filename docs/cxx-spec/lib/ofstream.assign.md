# 29.9.4.2 Assignment and swap [ofstream.assign]

[Contents](../README.md) · up: [29.9.4 Class template `basic_ofstream` \[ofstream\]](ofstream.md) · prev: [29.9.4.1 Constructors \[ofstream.cons\]](ofstream.cons.md) · next: [29.9.4.3 Member functions \[ofstream.members\]](ofstream.members.md)

```cpp
void swap(basic_ofstream& rhs);
```

**1** *Effects:* Exchanges the state of `*this` and `rhs` by calling `basic_ostream<charT, traits>::swap(rhs)` and `sb.swap(rhs.sb)`.

```cpp
template<class charT, class traits>
  void swap(basic_ofstream<charT, traits>& x,
            basic_ofstream<charT, traits>& y);
```

**2** *Effects:* Equivalent to: `x.swap(y)`.
