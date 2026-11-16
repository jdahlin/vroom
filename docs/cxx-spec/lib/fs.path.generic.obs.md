# 29.11.7.4.7 Generic format observers [fs.path.generic.obs]

[Contents](../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.6 Native format observers \[fs.path.native.obs\]](fs.path.native.obs.md) · next: [29.11.7.4.8 Compare \[fs.path.compare\]](fs.path.compare.md)

**1** Generic format observer functions return strings formatted according to the generic pathname format ([[fs.path.generic]](fs.path.generic.md)). A single slash (`'/'`) character is used as the *directory-separator*.

**2** [*Example 1*: On an operating system that uses backslash as its *preferred-separator*,

```cpp
path("foo\\bar").generic_string()
```

returns `"foo/bar"`. — *end example*]

```cpp
template<class EcharT, class traits = char_traits<EcharT>,
         class Allocator = allocator<EcharT>>
  basic_string<EcharT, traits, Allocator>
    generic_string(const Allocator& a = Allocator()) const;
```

**3** *Returns:* The pathname in the generic format.

**4** *Remarks:* All memory allocation, including for the return value, shall be performed by `a`. Conversion, if any, is specified by [[fs.path.cvt]](fs.path.cvt.md).

```cpp
std::string generic_string() const;
std::wstring generic_wstring() const;
std::u8string generic_u8string() const;
std::u16string generic_u16string() const;
std::u32string generic_u32string() const;
```

**5** *Returns:* The pathname in the generic format.

**6** *Remarks:* Conversion, if any, is specified by [[fs.path.cvt]](fs.path.cvt.md).
