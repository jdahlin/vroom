# 29.8.5.1 Constructors [stringstream.cons]

[Contents](../README.md) · up: [29.8.5 Class template `basic_stringstream` \[stringstream\]](stringstream.md) · prev: [29.8.5 Class template `basic_stringstream` \[stringstream\]](stringstream.md) · next: [29.8.5.2 Assignment and swap \[stringstream.assign\]](stringstream.assign.md)

```cpp
explicit basic_stringstream(ios_base::openmode which);
```

**1** *Effects:* Initializes the base class with `basic_iostream<charT, traits>(addressof(sb))` ([[iostream.cons]](iostream.cons.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(which)`.

```cpp
explicit basic_stringstream(
  const basic_string<charT, traits, Allocator>& s,
  ios_base::openmode which = ios_base::out | ios_base::in);
```

**2** *Effects:* Initializes the base class with `basic_iostream<charT, traits>(addressof(sb))` ([[iostream.cons]](iostream.cons.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(s, which)`.

```cpp
basic_stringstream(ios_base::openmode which, const Allocator& a);
```

**3** *Effects:* Initializes the base class with `basic_iostream<charT, traits>(addressof(sb))` ([[iostream.cons]](iostream.cons.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(which, a)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
explicit basic_stringstream(
  basic_string<charT, traits, Allocator>&& s,
  ios_base::openmode which = ios_base::out | ios_base::in);
```

**4** *Effects:* Initializes the base class with `basic_iostream<charT, traits>(addressof(sb))` ([[iostream.cons]](iostream.cons.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(std::move(s), which)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
template<class SAlloc>
  basic_stringstream(
    const basic_string<charT, traits, SAlloc>& s,
    ios_base::openmode which, const Allocator& a);
```

**5** *Effects:* Initializes the base class with `basic_iostream<charT, traits>(addressof(sb))` ([[iostream.cons]](iostream.cons.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(s, which, a)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
template<class SAlloc>
  explicit basic_stringstream(
    const basic_string<charT, traits, SAlloc>& s,
    ios_base::openmode which = ios_base::out | ios_base::in);
```

**6** *Constraints:* `is_same_v<SAlloc,Allocator>` is `false`.

**7** *Effects:* Initializes the base class with `basic_iostream<charT, traits>(addressof(sb))` ([[iostream.cons]](iostream.cons.md)) and `sb` with `basic_stringbuf<charT, traits, Allocator>(s, which)` ([[stringbuf.cons]](stringbuf.cons.md)).

```cpp
basic_stringstream(basic_stringstream&& rhs);
```

**8** *Effects:* Move constructs from the rvalue `rhs`. This is accomplished by move constructing the base class, and the contained `basic_stringbuf`. Then calls `basic_istream<charT, traits>::set_rdbuf(addressof(sb))` to install the contained `basic_stringbuf`.
