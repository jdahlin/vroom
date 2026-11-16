# 29.8.2.1 Constructors [stringbuf.cons]

[Contents](../README.md) · up: [29.8.2 Class template `basic_stringbuf` \[stringbuf\]](stringbuf.md) · prev: [29.8.2 Class template `basic_stringbuf` \[stringbuf\]](stringbuf.md) · next: [29.8.2.2 Assignment and swap \[stringbuf.assign\]](stringbuf.assign.md)

```cpp
explicit basic_stringbuf(ios_base::openmode which);
```

**1** *Effects:* Initializes the base class with `basic_streambuf()` ([[streambuf.cons]](streambuf.cons.md)), and `mode` with `which`. It is implementation-defined whether the sequence pointers (`eback()`, `gptr()`, `egptr()`, `pbase()`, `pptr()`, `epptr()`) are initialized to null pointers.

**2** *Postconditions:* `str().empty()` is `true`.

```cpp
explicit basic_stringbuf(
  const basic_string<charT, traits, Allocator>& s,
  ios_base::openmode which = ios_base::in | ios_base::out);
```

**3** *Effects:* Initializes the base class with `basic_streambuf()` ([[streambuf.cons]](streambuf.cons.md)), `mode` with `which`, and `buf` with `s`, then calls `init_buf_ptrs()`.

```cpp
basic_stringbuf(ios_base::openmode which, const Allocator &a);
```

**4** *Effects:* Initializes the base class with `basic_streambuf()` ([[streambuf.cons]](streambuf.cons.md)), `mode` with `which`, and `buf` with `a`, then calls `init_buf_ptrs()`.

**5** *Postconditions:* `str().empty()` is `true`.

```cpp
explicit basic_stringbuf(
  basic_string<charT, traits, Allocator>&& s,
  ios_base::openmode which = ios_base::in | ios_base::out);
```

**6** *Effects:* Initializes the base class with `basic_streambuf()` ([[streambuf.cons]](streambuf.cons.md)), `mode` with `which`, and `buf` with `std::move(s)`, then calls `init_buf_ptrs()`.

```cpp
template<class SAlloc>
  basic_stringbuf(
    const basic_string<charT, traits, SAlloc>& s,
    ios_base::openmode which, const Allocator &a);
```

**7** *Effects:* Initializes the base class with `basic_streambuf()` ([[streambuf.cons]](streambuf.cons.md)), `mode` with `which`, and `buf` with `{s,a}`, then calls `init_buf_ptrs()`.

```cpp
template<class SAlloc>
  explicit basic_stringbuf(
    const basic_string<charT, traits, SAlloc>& s,
    ios_base::openmode which = ios_base::in | ios_base::out);
```

**8** *Constraints:* `is_same_v<SAlloc,Allocator>` is `false`.

**9** *Effects:* Initializes the base class with `basic_streambuf()` ([[streambuf.cons]](streambuf.cons.md)), `mode` with `which`, and `buf` with `s`, then calls `init_buf_ptrs()`.

```cpp
basic_stringbuf(basic_stringbuf&& rhs);
basic_stringbuf(basic_stringbuf&& rhs, const Allocator& a);
```

**10** *Effects:* Copy constructs the base class from `rhs` and initializes `mode` with `rhs.mode`. In the first form `buf` is initialized from `std::move(rhs).str()`. In the second form `buf` is initialized from `{std::move(rhs).str(), a}`. It is implementation-defined whether the sequence pointers in `*this` (`eback()`, `gptr()`, `egptr()`, `pbase()`, `pptr()`, `epptr()`) obtain the values which `rhs` had.

**11** *Postconditions:* Let `rhs_p` refer to the state of `rhs` just prior to this construction and let `rhs_a` refer to the state of `rhs` just after this construction.

- `str() == rhs_p.str()`
- `gptr() - eback() == rhs_p.gptr() - rhs_p.eback()`
- `egptr() - eback() == rhs_p.egptr() - rhs_p.eback()`
- `pptr() - pbase() == rhs_p.pptr() - rhs_p.pbase()`
- `epptr() - pbase() == rhs_p.epptr() - rhs_p.pbase()`
- `if (eback()) eback() != rhs_a.eback()`
- `if (gptr()) gptr() != rhs_a.gptr()`
- `if (egptr()) egptr() != rhs_a.egptr()`
- `if (pbase()) pbase() != rhs_a.pbase()`
- `if (pptr()) pptr() != rhs_a.pptr()`
- `if (epptr()) epptr() != rhs_a.epptr()`
- `getloc() == rhs_p.getloc()`
- `rhs` is empty but usable, as if `std::move(rhs).str()` was called.
