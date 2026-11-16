# 29.8.4.1 Constructors [ostringstream.cons]

[Contents](../../README.md) · up: [29.8.4 Class template `basic_ostringstream` \[ostringstream\]](ostringstream.md) · prev: [29.8.4 Class template `basic_ostringstream` \[ostringstream\]](ostringstream.md) · next: [29.8.4.2 Assignment and swap \[ostringstream.assign\]](ostringstream.assign.md)

```cpp
explicit basic_ostringstream(ios_base::openmode which);
```

**1** *Effects:* Initializes the base class with `basic_ostream<charT, traits>(addressof(sb))` ([[ostream]](ostream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(which | ios_base::out)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
explicit basic_ostringstream(
  const basic_string<charT, traits, Allocator>& s,
  ios_base::openmode which = ios_base::out);
```

**2** *Effects:* Initializes the base class with `basic_ostream<charT, traits>(addressof(sb))` ([[ostream]](ostream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(s, which | ios_base::out)`([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
basic_ostringstream(ios_base::openmode which, const Allocator& a);
```

**3** *Effects:* Initializes the base class with `basic_ostream<charT, traits>(addressof(sb))` ([[ostream]](ostream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(which | ios_base::out, a)`([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
explicit basic_ostringstream(
  basic_string<charT, traits, Allocator>&& s,
  ios_base::openmode which = ios_base::out);
```

**4** *Effects:* Initializes the base class with `basic_ostream<charT, traits>(addressof(sb))` ([[ostream]](ostream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(std::move(s), which | ios_base::out)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
template<class SAlloc>
  basic_ostringstream(
    const basic_string<charT, traits, SAlloc>& s,
    ios_base::openmode which, const Allocator& a);
```

**5** *Effects:* Initializes the base class with `basic_ostream<charT, traits>(addressof(sb))` ([[ostream]](ostream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(s, which | ios_base::out, a)`([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
template<class SAlloc>
  explicit basic_ostringstream(
    const basic_string<charT, traits, SAlloc>& s,
    ios_base::openmode which = ios_base::out);
```

**6** *Constraints:* `is_same_v<SAlloc,Allocator>` is `false`.

**7** *Effects:* Initializes the base class with `basic_ostream<charT, traits>(addressof(sb))` ([[ostream]](ostream.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(s, which | ios_base::out)`([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
basic_ostringstream(basic_ostringstream&& rhs);
```

**8** *Effects:* Move constructs from the rvalue `rhs`. This is accomplished by move constructing the base class, and the contained `basic_stringbuf`. Then calls `basic_ostream<charT, traits>::set_rdbuf(addressof(sb))` to install the contained `basic_stringbuf`.
