# D.23 Deprecated filesystem path factory functions [depr.fs.path.factory]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.22 Deprecated locale category facets \[depr.locale.category\]](depr.locale.category.md) · next: [D.24 Deprecated atomic operations \[depr.atomics\]](depr.atomics.md)

```cpp
template<class Source>
  path u8path(const Source& source);
template<class InputIterator>
  path u8path(InputIterator first, InputIterator last);
```

**1** *Requires:* The `source` and `[first, last)` sequences are UTF-8 encoded. The value type of `Source` and `InputIterator` is `char` or `char8_t`. `Source` meets the requirements specified in [[fs.path.req]](../lib/io/fs.path.req.md).

**2** *Returns:*

- If `value_type` is `char` and the current native narrow encoding ([[fs.path.type.cvt]](../lib/io/fs.path.type.cvt.md)) is UTF-8, return `path(source)` or `path(first, last)`; otherwise,
- if `value_type` is `wchar_t` and the native wide encoding is UTF-16, or if `value_type` is `char16_t` or `char32_t`, convert `source` or `[first, last)` to a temporary, `tmp`, of type `string_type` and return `path(tmp)`; otherwise,
- convert `source` or `[first, last)` to a temporary, `tmp`, of type `u32string` and return `path(tmp)`.

**3** *Remarks:* Argument format conversion ([[fs.path.fmt.cvt]](../lib/io/fs.path.fmt.cvt.md)) applies to the arguments for these functions. How Unicode encoding conversions are performed is unspecified.

**4** [*Example 1*: A string is to be read from a database that is encoded in UTF-8, and used to create a directory using the native encoding for filenames:

```cpp
namespace fs = std::filesystem;
std::string utf8_string = read_utf8_data();
fs::create_directory(fs::u8path(utf8_string));
```

For POSIX-based operating systems with the native narrow encoding set to UTF-8, no encoding or type conversion occurs.

For POSIX-based operating systems with the native narrow encoding not set to UTF-8, a conversion to UTF-32 occurs, followed by a conversion to the current native narrow encoding. Some Unicode characters may have no native character set representation.

For Windows-based operating systems a conversion from UTF-8 to UTF-16 occurs. — *end example*] [*Note 1*: The example above is representative of a historical use of `filesystem::u8path`. Passing a `std::u8string` to `path`’s constructor is preferred for an indication of UTF-8 encoding more consistent with `path`’s handling of other encodings. — *end note*]
