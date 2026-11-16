# 29.8.5.3 Member functions [stringstream.members]

[Contents](../README.md) · up: [29.8.5 Class template `basic_stringstream` \[stringstream\]](stringstream.md) · prev: [29.8.5.2 Assignment and swap \[stringstream.assign\]](stringstream.assign.md) · next: [29.9 File-based streams \[file.streams\]](file.streams.md)

```cpp
basic_stringbuf<charT, traits, Allocator>* rdbuf() const;
```

**1** *Returns:* `const_cast<basic_stringbuf<charT, traits, Allocator>*>(addressof(sb))`.

```cpp
basic_string<charT, traits, Allocator> str() const &;
```

**2** *Effects:* Equivalent to: `return rdbuf()->str();`

```cpp
template<class SAlloc>
  basic_string<charT,traits,SAlloc> str(const SAlloc& sa) const;
```

**3** *Effects:* Equivalent to: `return rdbuf()->str(sa);`

```cpp
basic_string<charT,traits,Allocator> str() &&;
```

**4** *Effects:* Equivalent to: `return std::move(*rdbuf()).str();`

```cpp
basic_string_view<charT, traits> view() const noexcept;
```

**5** *Effects:* Equivalent to: `return rdbuf()->view();`

```cpp
void str(const basic_string<charT, traits, Allocator>& s);
```

**6** *Effects:* Equivalent to: `rdbuf()->str(s);`

```cpp
template<class SAlloc>
  void str(const basic_string<charT, traits, SAlloc>& s);
```

**7** *Effects:* Equivalent to: `rdbuf()->str(s);`

```cpp
void str(basic_string<charT, traits, Allocator>&& s);
```

**8** *Effects:* Equivalent to: `rdbuf()->str(std::move(s));`
