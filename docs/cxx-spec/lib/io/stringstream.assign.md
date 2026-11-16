# 29.8.5.2 Assignment and swap [stringstream.assign]

[Contents](../../README.md) · up: [29.8.5 Class template `basic_stringstream` \[stringstream\]](stringstream.md) · prev: [29.8.5.1 Constructors \[stringstream.cons\]](stringstream.cons.md) · next: [29.8.5.3 Member functions \[stringstream.members\]](stringstream.members.md)

```cpp
void swap(basic_stringstream& rhs);
```

**1** *Effects:* Equivalent to:

```cpp
basic_iostream<charT,traits>::swap(rhs);
sb.swap(rhs.sb);
```

```cpp
template<class charT, class traits, class Allocator>
  void swap(basic_stringstream<charT, traits, Allocator>& x,
            basic_stringstream<charT, traits, Allocator>& y);
```

**2** *Effects:* Equivalent to: `x.swap(y)`.
