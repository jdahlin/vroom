# 21.3.3.5 Erasure [string.erasure]

[Contents](../../README.md) · up: [21.3.3 Non-member functions \[string.nonmembers\]](string.nonmembers.md) · prev: [21.3.3.4 Inserters and extractors \[string.io\]](string.io.md) · next: [21.3.4 Numeric conversions \[string.conversions\]](string.conversions.md)

```cpp
template<class charT, class traits, class Allocator, class U>
  constexpr typename basic_string<charT, traits, Allocator>::size_type
    erase(basic_string<charT, traits, Allocator>& c, const U& value);
```

**1** *Effects:* Equivalent to:

```cpp
auto it = remove(c.begin(), c.end(), value);
auto r = distance(it, c.end());
c.erase(it, c.end());
return r;
```

```cpp
template<class charT, class traits, class Allocator, class Predicate>
  constexpr typename basic_string<charT, traits, Allocator>::size_type
    erase_if(basic_string<charT, traits, Allocator>& c, Predicate pred);
```

**2** *Effects:* Equivalent to:

```cpp
auto it = remove_if(c.begin(), c.end(), pred);
auto r = distance(it, c.end());
c.erase(it, c.end());
return r;
```
