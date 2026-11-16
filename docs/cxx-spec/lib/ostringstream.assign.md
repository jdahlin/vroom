# 29.8.4.2 Assignment and swap [ostringstream.assign]

[Contents](../README.md) · up: [29.8.4 Class template `basic_ostringstream` \[ostringstream\]](ostringstream.md) · prev: [29.8.4.1 Constructors \[ostringstream.cons\]](ostringstream.cons.md) · next: [29.8.4.3 Member functions \[ostringstream.members\]](ostringstream.members.md)

```cpp
void swap(basic_ostringstream& rhs);
```

**1** *Effects:* Equivalent to:

```cpp
basic_ostream<charT, traits>::swap(rhs);
sb.swap(rhs.sb);
```

```cpp
template<class charT, class traits, class Allocator>
  void swap(basic_ostringstream<charT, traits, Allocator>& x,
            basic_ostringstream<charT, traits, Allocator>& y);
```

**2** *Effects:* Equivalent to: `x.swap(y)`.
