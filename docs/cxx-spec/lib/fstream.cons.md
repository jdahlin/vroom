# 29.9.5.1 Constructors [fstream.cons]

[Contents](../README.md) · up: [29.9.5 Class template `basic_fstream` \[fstream\]](fstream.md) · prev: [29.9.5 Class template `basic_fstream` \[fstream\]](fstream.md) · next: [29.9.5.2 Assignment and swap \[fstream.assign\]](fstream.assign.md)

```cpp
basic_fstream();
```

**1** *Effects:* Initializes the base class with `basic_iostream<charT, traits>(addressof(sb))` ([[iostream.cons]](iostream.cons.md)) and `sb` with `basic_filebuf<charT, traits>()`.

```cpp
explicit basic_fstream(
  const char* s,
  ios_base::openmode mode = ios_base::in | ios_base::out);
explicit basic_fstream(
  const filesystem::path::value_type* s,
  ios_base::openmode mode = ios_base::in | ios_base::out);  // wide systems only; see [fstream.syn]
```

**2** *Effects:* Initializes the base class with `basic_iostream<charT, traits>(addressof(sb))` ([[iostream.cons]](iostream.cons.md)) and `sb` with `basic_filebuf<charT, traits>()`. Then calls `rdbuf()->open(s, mode)`. If that function returns a null pointer, calls `setstate(failbit)`.

```cpp
explicit basic_fstream(
  const string& s,
  ios_base::openmode mode = ios_base::in | ios_base::out);
explicit basic_fstream(
  const filesystem::path& s,
  ios_base::openmode mode = ios_base::in | ios_base::out);
```

**3** *Effects:* Equivalent to: `basic_fstream(s.c_str(), mode)`.

```cpp
basic_fstream(basic_fstream&& rhs);
```

**4** *Effects:* Move constructs the base class, and the contained `basic_filebuf`. Then calls `basic_istream<charT, traits>::set_rdbuf(addressof(sb))` to install the contained `basic_filebuf`.
