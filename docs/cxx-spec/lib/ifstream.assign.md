# 29.9.3.2 Assignment and swap [ifstream.assign]

[Contents](../README.md) · up: [29.9.3 Class template `basic_ifstream` \[ifstream\]](ifstream.md) · prev: [29.9.3.1 Constructors \[ifstream.cons\]](ifstream.cons.md) · next: [29.9.3.3 Member functions \[ifstream.members\]](ifstream.members.md)

```cpp
void swap(basic_ifstream& rhs);
```

**1** *Effects:* Exchanges the state of `*this` and `rhs` by calling `basic_istream<charT, traits>::swap(rhs)` and `sb.swap(rhs.sb)`.

```cpp
template<class charT, class traits>
  void swap(basic_ifstream<charT, traits>& x,
            basic_ifstream<charT, traits>& y);
```

**2** *Effects:* Equivalent to: `x.swap(y)`.
