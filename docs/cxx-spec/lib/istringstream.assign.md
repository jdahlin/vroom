# 29.8.3.2 Assignment and swap [istringstream.assign]

[Contents](../README.md) · up: [29.8.3 Class template `basic_istringstream` \[istringstream\]](istringstream.md) · prev: [29.8.3.1 Constructors \[istringstream.cons\]](istringstream.cons.md) · next: [29.8.3.3 Member functions \[istringstream.members\]](istringstream.members.md)

```cpp
void swap(basic_istringstream& rhs);
```

**1** *Effects:* Equivalent to:

```cpp
basic_istream<charT, traits>::swap(rhs);
sb.swap(rhs.sb);
```

```cpp
template<class charT, class traits, class Allocator>
  void swap(basic_istringstream<charT, traits, Allocator>& x,
            basic_istringstream<charT, traits, Allocator>& y);
```

**2** *Effects:* Equivalent to: `x.swap(y)`.
