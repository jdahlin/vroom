# 29.9.4.1 Constructors [ofstream.cons]

[Contents](../../README.md) · up: [29.9.4 Class template `basic_ofstream` \[ofstream\]](ofstream.md) · prev: [29.9.4 Class template `basic_ofstream` \[ofstream\]](ofstream.md) · next: [29.9.4.2 Assignment and swap \[ofstream.assign\]](ofstream.assign.md)

```cpp
basic_ofstream();
```

**1** *Effects:* Initializes the base class with `basic_ostream<charT, traits>(addressof(sb))` ([[ostream.cons]](ostream.cons.md)) and `sb` with `basic_filebuf<charT, traits>()` ([[filebuf.cons]](filebuf.cons.md)).

```cpp
explicit basic_ofstream(const char* s,
                        ios_base::openmode mode = ios_base::out);
explicit basic_ofstream(const filesystem::path::value_type* s,
                        ios_base::openmode mode = ios_base::out); // wide systems only; see [fstream.syn]
```

**2** *Effects:* Initializes the base class with `basic_ostream<charT, traits>(addressof(sb))` ([[ostream.cons]](ostream.cons.md)) and `sb` with `basic_filebuf<charT, traits>()` ([[filebuf.cons]](filebuf.cons.md)), then calls `rdbuf()->open(s, mode | ios_base::out)`. If that function returns a null pointer, calls `setstate(failbit)`.

```cpp
explicit basic_ofstream(const string& s,
                        ios_base::openmode mode = ios_base::out);
explicit basic_ofstream(const filesystem::path& s,
                        ios_base::openmode mode = ios_base::out);
```

**3** *Effects:* Equivalent to: `basic_ofstream(s.c_str(), mode)`.

```cpp
basic_ofstream(basic_ofstream&& rhs);
```

**4** *Effects:* Move constructs the base class, and the contained `basic_filebuf`. Then calls `basic_ostream<charT, traits>::set_rdbuf(addressof(sb))` to install the contained `basic_filebuf`.
