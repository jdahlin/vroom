# 21.3.3.1 `operator+` [string.op.plus]

[Contents](../README.md) · up: [21.3.3 Non-member functions \[string.nonmembers\]](string.nonmembers.md) · prev: [21.3.3 Non-member functions \[string.nonmembers\]](string.nonmembers.md) · next: [21.3.3.2 Non-member comparison functions \[string.cmp\]](string.cmp.md)

```cpp
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(const basic_string<charT, traits, Allocator>& lhs,
              const basic_string<charT, traits, Allocator>& rhs);
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(const basic_string<charT, traits, Allocator>& lhs, const charT* rhs);
```

**1** *Effects:* Equivalent to:

```cpp
basic_string<charT, traits, Allocator> r = lhs;
r.append(rhs);
return r;
```

```cpp
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(basic_string<charT, traits, Allocator>&& lhs,
              const basic_string<charT, traits, Allocator>& rhs);
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(basic_string<charT, traits, Allocator>&& lhs, const charT* rhs);
```

**2** *Effects:* Equivalent to:

```cpp
lhs.append(rhs);
return std::move(lhs);
```

```cpp
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(basic_string<charT, traits, Allocator>&& lhs,
              basic_string<charT, traits, Allocator>&& rhs);
```

**3** *Effects:* Equivalent to:

```cpp
lhs.append(rhs);
return std::move(lhs);
```

except that both `lhs` and `rhs` are left in valid but unspecified states. [*Note 1*: If `lhs` and `rhs` have equal allocators, the implementation may move from either. — *end note*]

```cpp
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(const basic_string<charT, traits, Allocator>& lhs,
              basic_string<charT, traits, Allocator>&& rhs);
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(const charT* lhs, basic_string<charT, traits, Allocator>&& rhs);
```

**4** *Effects:* Equivalent to:

```cpp
rhs.insert(0, lhs);
return std::move(rhs);
```

```cpp
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(const charT* lhs, const basic_string<charT, traits, Allocator>& rhs);
```

**5** *Effects:* Equivalent to:

```cpp
basic_string<charT, traits, Allocator> r = rhs;
r.insert(0, lhs);
return r;
```

```cpp
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(charT lhs, const basic_string<charT, traits, Allocator>& rhs);
```

**6** *Effects:* Equivalent to:

```cpp
basic_string<charT, traits, Allocator> r = rhs;
r.insert(r.begin(), lhs);
return r;
```

```cpp
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(charT lhs, basic_string<charT, traits, Allocator>&& rhs);
```

**7** *Effects:* Equivalent to:

```cpp
rhs.insert(rhs.begin(), lhs);
return std::move(rhs);
```

```cpp
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(const basic_string<charT, traits, Allocator>& lhs, charT rhs);
```

**8** *Effects:* Equivalent to:

```cpp
basic_string<charT, traits, Allocator> r = lhs;
r.push_back(rhs);
return r;
```

```cpp
template<class charT, class traits, class Allocator>
  constexpr basic_string<charT, traits, Allocator>
    operator+(basic_string<charT, traits, Allocator>&& lhs, charT rhs);
```

**9** *Effects:* Equivalent to:

```cpp
lhs.push_back(rhs);
return std::move(lhs);
```
