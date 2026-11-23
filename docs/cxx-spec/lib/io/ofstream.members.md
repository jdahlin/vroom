# 29.9.4.3 Member functions [ofstream.members]

[Contents](../../README.md) · up: [29.9.4 Class template `basic_ofstream` \[ofstream\]](ofstream.md) · prev: [29.9.4.2 Assignment and swap \[ofstream.assign\]](ofstream.assign.md) · next: [29.9.5 Class template `basic_fstream` \[fstream\]](fstream.md)

```cpp
basic_filebuf<charT, traits>* rdbuf() const;
```

**1** *Returns:* `const_cast<basic_filebuf<charT, traits>*>(addressof(sb))`.

```cpp
bool is_open() const;
```

**2** *Returns:* `rdbuf()->is_open()`.

```cpp
void open(const char* s, ios_base::openmode mode = ios_base::out);
void open(const filesystem::path::value_type* s,
          ios_base::openmode mode = ios_base::out);             // wide systems only; see [fstream.syn]
```

**3** *Effects:* Calls `rdbuf()->open(s, mode | ios_base::out)`. If that function does not return a null pointer calls `clear()`, otherwise calls `setstate(failbit)` (which may throw `ios_base::failure`) ([[iostate.flags]](iostate.flags.md)).

```cpp
void close();
```

**4** *Effects:* Calls `rdbuf()->close()` and, if that function fails (returns a null pointer), calls `setstate(failbit)` (which may throw `ios_base::failure`) ([[iostate.flags]](iostate.flags.md)).

```cpp
void open(const string& s, ios_base::openmode mode = ios_base::out);
void open(const filesystem::path& s, ios_base::openmode mode = ios_base::out);
```

**5** *Effects:* Calls `open(s.c_str(), mode)`.
