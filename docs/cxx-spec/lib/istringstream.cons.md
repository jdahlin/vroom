# 29.8.3.1 Constructors [istringstream.cons]

[Contents](../README.md) · up: [29.8.3 Class template `basic_istringstream` \[istringstream\]](istringstream.md) · prev: [29.8.3 Class template `basic_istringstream` \[istringstream\]](istringstream.md) · next: [29.8.3.2 Assignment and swap \[istringstream.assign\]](istringstream.assign.md)

```cpp
explicit basic_istringstream(ios_base::openmode which);
```

**1** *Effects:* Initializes the base class with `basic_istream<charT, traits>(addressof(sb))` ([[istream]](istream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(which | ios_base::in)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
explicit basic_istringstream(
  const basic_string<charT, traits, Allocator>& s,
  ios_base::openmode which = ios_base::in);
```

**2** *Effects:* Initializes the base class with `basic_istream<charT, traits>(addressof(sb))` ([[istream]](istream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(s, which | ios_base::in)`([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
basic_istringstream(ios_base::openmode which, const Allocator& a);
```

**3** *Effects:* Initializes the base class with `basic_istream<charT, traits>(addressof(sb))` ([[istream]](istream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(which | ios_base::in, a)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
explicit basic_istringstream(
  basic_string<charT, traits, Allocator>&& s,
  ios_base::openmode which = ios_base::in);
```

**4** *Effects:* Initializes the base class with `basic_istream<charT, traits>(addressof(sb))` ([[istream]](istream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(std::move(s), which | ios_base::in)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
template<class SAlloc>
  basic_istringstream(
    const basic_string<charT, traits, SAlloc>& s,
    ios_base::openmode which, const Allocator& a);
```

**5** *Effects:* Initializes the base class with `basic_istream<charT, traits>(addressof(sb))` ([[istream]](istream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(s, which | ios_base::in, a)`([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
template<class SAlloc>
  explicit basic_istringstream(
    const basic_string<charT, traits, SAlloc>& s,
    ios_base::openmode which = ios_base::in);
```

**6** *Effects:* Initializes the base class with `basic_istream<charT, traits>(addressof(sb))` ([[istream]](istream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(s, which | ios_base::in)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
basic_istringstream(basic_istringstream&& rhs);
```

**7** *Effects:* Move constructs from the rvalue `rhs`. This is accomplished by move constructing the base class, and the contained `basic_stringbuf`. Then calls `basic_istream<charT, traits>::set_rdbuf(addressof(sb))` to install the contained `basic_stringbuf`.
