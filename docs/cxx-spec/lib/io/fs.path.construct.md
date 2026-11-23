# 29.11.7.4.1 Constructors [fs.path.construct]

[Contents](../../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · next: [29.11.7.4.2 Assignments \[fs.path.assign\]](fs.path.assign.md)

```cpp
path() noexcept;
```

**1** *Postconditions:* `empty() == true`.

```cpp
path(const path& p);
path(path&& p) noexcept;
```

**2** *Effects:* Constructs an object of class `path` having the same pathname in the native and generic formats, respectively, as the original value of `p`. In the second form, `p` is left in a valid but unspecified state.

```cpp
path(string_type&& source, format fmt = auto_format);
```

**3** *Effects:* Constructs an object of class `path` for which the pathname in the detected-format of `source` has the original value of `source` ([[fs.path.fmt.cvt]](fs.path.fmt.cvt.md)), converting format if required ([[fs.path.fmt.cvt]](fs.path.fmt.cvt.md)). `source` is left in a valid but unspecified state.

```cpp
template<class Source>
  path(const Source& source, format fmt = auto_format);
template<class InputIterator>
  path(InputIterator first, InputIterator last, format fmt = auto_format);
```

**4** *Effects:* Let `s` be the effective range of `source` ([[fs.path.req]](fs.path.req.md)) or the range `[first, last)`, with the encoding converted if required ([[fs.path.cvt]](fs.path.cvt.md)). Finds the detected-format of `s` ([[fs.path.fmt.cvt]](fs.path.fmt.cvt.md)) and constructs an object of class `path` for which the pathname in that format is `s`.

```cpp
template<class Source>
  path(const Source& source, const locale& loc, format fmt = auto_format);
template<class InputIterator>
  path(InputIterator first, InputIterator last, const locale& loc, format fmt = auto_format);
```

**5** *Mandates:* The value type of `Source` and `InputIterator` is `char`.

**6** *Effects:* Let `s` be the effective range of `source` or the range `[first, last)`, after converting the encoding as follows:

- If `value_type` is `wchar_t`, converts to the native wide encoding ([[fs.path.type.cvt]](fs.path.type.cvt.md)) using the `codecvt<wchar_t, char, mbstate_t>` facet of `loc`.
- Otherwise a conversion is performed using the `codecvt<wchar_t, char, mbstate_t>` facet of `loc`, and then a second conversion to the current ordinary encoding.

**7** Finds the detected-format of `s` ([[fs.path.fmt.cvt]](fs.path.fmt.cvt.md)) and constructs an object of class `path` for which the pathname in that format is `s`.

[*Example 1*: A string is to be read from a database that is encoded in ISO/IEC 8859-1, and used to create a directory:

```cpp
namespace fs = std::filesystem;
std::string latin1_string = read_latin1_data();
codecvt_8859_1<wchar_t> latin1_facet;
std::locale latin1_locale(std::locale(), latin1_facet);
fs::create_directory(fs::path(latin1_string, latin1_locale));
```

For POSIX-based operating systems, the path is constructed by first using `latin1_facet` to convert ISO/IEC 8859-1 encoded `latin1_string` to a wide character string in the native wide encoding ([[fs.path.type.cvt]](fs.path.type.cvt.md)). The resulting wide string is then converted to an ordinary character pathname string in the current native ordinary encoding. If the native wide encoding is UTF-16 or UTF-32, and the current native ordinary encoding is UTF-8, all of the characters in the ISO/IEC 8859-1 character set will be converted to their Unicode representation, but for other native ordinary encodings some characters may have no representation.

For Windows-based operating systems, the path is constructed by using `latin1_facet` to convert ISO/IEC 8859-1 encoded `latin1_string` to a UTF-16 encoded wide character pathname string. All of the characters in the ISO/IEC 8859-1 character set will be converted to their Unicode representation. — *end example*]
