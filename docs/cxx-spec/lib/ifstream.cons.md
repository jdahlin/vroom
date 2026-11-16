# 29.9.3.1 Constructors [ifstream.cons]

[Contents](../README.md) · up: [29.9.3 Class template `basic_ifstream` \[ifstream\]](ifstream.md) · prev: [29.9.3 Class template `basic_ifstream` \[ifstream\]](ifstream.md) · next: [29.9.3.2 Assignment and swap \[ifstream.assign\]](ifstream.assign.md)

```cpp
basic_ifstream();
```

**1** *Effects:* Initializes the base class with `basic_istream<charT, traits>(addressof(sb))` ([[istream.cons]](istream.cons.md)) and `sb` with `basic_filebuf<charT, traits>()` ([[filebuf.cons]](filebuf.cons.md)).

```cpp
explicit basic_ifstream(const char* s,
                        ios_base::openmode mode = ios_base::in);
explicit basic_ifstream(const filesystem::path::value_type* s,
                        ios_base::openmode mode = ios_base::in);  // wide systems only; see [fstream.syn]
```

**2** *Effects:* Initializes the base class with `basic_istream<charT, traits>(addressof(sb))` ([[istream.cons]](istream.cons.md)) and `sb` with `basic_filebuf<charT, traits>()` ([[filebuf.cons]](filebuf.cons.md)), then calls `rdbuf()->open(s, mode | ios_base::in)`. If that function returns a null pointer, calls `setstate(failbit)`.

```cpp
explicit basic_ifstream(const string& s,
                        ios_base::openmode mode = ios_base::in);
explicit basic_ifstream(const filesystem::path& s,
                        ios_base::openmode mode = ios_base::in);
```

**3** *Effects:* Equivalent to: `basic_ifstream(s.c_str(), mode)`.

```cpp
basic_ifstream(basic_ifstream&& rhs);
```

**4** *Effects:* Move constructs the base class, and the contained `basic_filebuf`. Then calls `basic_istream<charT, traits>::set_rdbuf(addressof(sb))` to install the contained `basic_filebuf`.
