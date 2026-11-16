# 29.9.2.3 Member functions [filebuf.members]

[Contents](../README.md) · up: [29.9.2 Class template `basic_filebuf` \[filebuf\]](filebuf.md) · prev: [29.9.2.2 Assignment and swap \[filebuf.assign\]](filebuf.assign.md) · next: [29.9.2.4 Overridden virtual functions \[filebuf.virtuals\]](filebuf.virtuals.md)

```cpp
bool is_open() const;
```

**1** *Returns:* `true` if a previous call to `open` succeeded (returned a non-null value) and there has been no intervening call to close.

```cpp
basic_filebuf* open(const char* s, ios_base::openmode mode);
basic_filebuf* open(const filesystem::path::value_type* s,
                    ios_base::openmode mode);  // wide systems only; see [fstream.syn]
```

**2** *Preconditions:* `s` points to a NTCTS ([[defns.ntcts]](definitions.md#defns.ntcts)).

**3** *Effects:* If `is_open() != false`, returns a null pointer. Otherwise, initializes the `filebuf` as required. It then opens the file to which `s` resolves, if possible, as if by a call to `fopen` with the second argument determined from `mode & ~ios_base::ate` as indicated in [Table 126](filebuf.members.md#tab:filebuf.open.modes). If `mode` is not some combination of flags shown in the table then the open fails.

<a id="tab:filebuf.open.modes"></a>
**Table 126: File open modes** [tab:filebuf.open.modes]

|  |  |  |  |  |  |
| --- | --- | --- | --- | --- | --- |
| `ios_base` flag combination | `stdio` equivalent |  |  |  |  |
| `binary` | `in` | `out` | `trunc` | `app` |  |
|  |  | + |  |  | `"w"` |
|  |  | + |  | + | `"a"` |
|  |  |  |  | + | `"a"` |
|  |  | + | + |  | `"w"` |
|  | + |  |  |  | `"r"` |
|  | + | + |  |  | `"r+"` |
|  | + | + | + |  | `"w+"` |
|  | + | + |  | + | `"a+"` |
|  | + |  |  | + | `"a+"` |
| + |  | + |  |  | `"wb"` |
| + |  | + |  | + | `"ab"` |
| + |  |  |  | + | `"ab"` |
| + |  | + | + |  | `"wb"` |
| + | + |  |  |  | `"rb"` |
| + | + | + |  |  | `"r+b"` |
| + | + | + | + |  | `"w+b"` |
| + | + | + |  | + | `"a+b"` |
| + | + |  |  | + | `"a+b"` |

**4** If the open operation succeeds and `ios_base::ate` is set in `mode`, positions the file to the end (as if by calling `fseek(file, 0, SEEK_END)`, where `file` is the pointer returned by calling `fopen`).[^1]

**5** If the repositioning operation fails, calls `close()` and returns a null pointer to indicate failure.

**6** *Returns:* `this` if successful, a null pointer otherwise.

```cpp
basic_filebuf* open(const string& s, ios_base::openmode mode);
basic_filebuf* open(const filesystem::path& s, ios_base::openmode mode);
```

**7** *Returns:* `open(s.c_str(), mode);`

```cpp
basic_filebuf* close();
```

**8** *Effects:* If `is_open() == false`, returns a null pointer. If a put area exists, calls `overflow(traits::eof())` to flush characters. If the last virtual member function called on `*this` (between `underflow`, `overflow`, `seekoff`, and `seekpos`) was `overflow` then calls `a_codecvt.unshift` (possibly several times) to determine a termination sequence, inserts those characters and calls `overflow(traits::eof())` again. Finally, regardless of whether any of the preceding calls fails or throws an exception, the function closes the file (as if by calling `fclose(file)`). If any of the calls made by the function, including `fclose`, fails, `close` fails by returning a null pointer. If one of these calls throws an exception, the exception is caught and rethrown after closing the file.

**9** *Returns:* `this` on success, a null pointer otherwise.

**10** *Postconditions:* `is_open() == false`.

[^1]: The macro `SEEK_END` is defined, and the function signatures `fopen(const char*, const char*)` and `fseek(FILE*, long, int)` are declared, in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).
