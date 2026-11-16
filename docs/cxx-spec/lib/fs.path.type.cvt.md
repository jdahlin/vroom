# 29.11.7.2.2 Type and encoding conversions [fs.path.type.cvt]

[Contents](../README.md) · up: [29.11.7.2 Conversions \[fs.path.cvt\]](fs.path.cvt.md) · prev: [29.11.7.2.1 Argument format conversions \[fs.path.fmt.cvt\]](fs.path.fmt.cvt.md) · next: [29.11.7.3 Requirements \[fs.path.req\]](fs.path.req.md)

**1** The *native encoding* of an ordinary character string is the operating system dependent current encoding for pathnames ([[fs.class.path]](fs.class.path.md)). The *native encoding* for wide character strings is the implementation-defined execution wide-character set encoding ([[lex.charset]](../lex.charset.md)).

**2** For member function arguments that take character sequences representing paths and for member functions returning strings, value type and encoding conversion is performed if the value type of the argument or return value differs from `path::value_type`. For the argument or return value, the method of conversion and the encoding to be converted to is determined by its value type:

- `char`: The encoding is the native ordinary encoding. The method of conversion, if any, is operating system dependent. [*Note 1*: For POSIX-based operating systems `path::value_type` is `char` so no conversion from `char` value type arguments or to `char` value type return values is performed. For Windows-based operating systems, the native ordinary encoding is determined by calling a Windows API function. — *end note*] [*Note 2*: This results in behavior identical to other C and C++ standard library functions that perform file operations using ordinary character strings to identify paths. Changing this behavior would be surprising and error prone. — *end note*]
- `wchar_t`: The encoding is the native wide encoding. The method of conversion is unspecified. [*Note 3*: For Windows-based operating systems `path::value_type` is `wchar_t` so no conversion from `wchar_t` value type arguments or to `wchar_t` value type return values is performed. — *end note*]
- `char8_t`: The encoding is UTF-8. The method of conversion is unspecified.
- `char16_t`: The encoding is UTF-16. The method of conversion is unspecified.
- `char32_t`: The encoding is UTF-32. The method of conversion is unspecified.

**3** If the encoding being converted to has no representation for source characters, the resulting converted characters, if any, are unspecified. Implementations should not modify member function arguments if already of type `path::value_type`.
