# 29.11.7.4.6 Native format observers [fs.path.native.obs]

[Contents](../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.5 Modifiers \[fs.path.modifiers\]](fs.path.modifiers.md) · next: [29.11.7.4.7 Generic format observers \[fs.path.generic.obs\]](fs.path.generic.obs.md)

**1** The string returned by all native format observers is in the native pathname format ([[fs.class.path]](fs.class.path.md)).

```cpp
const string_type& native() const noexcept;
```

**2** *Returns:* The pathname in the native format.

```cpp
const value_type* c_str() const noexcept;
```

**3** *Effects:* Equivalent to: `return native().c_str();`

```cpp
operator string_type() const;
```

**4** *Returns:* `native()`.

**5** [*Note 1*: Conversion to `string_type` is provided so that an object of class `path` can be given as an argument to existing standard library file stream constructors and open functions. — *end note*]

```cpp
template<class EcharT, class traits = char_traits<EcharT>,
         class Allocator = allocator<EcharT>>
  basic_string<EcharT, traits, Allocator>
    string(const Allocator& a = Allocator()) const;
```

**6** *Returns:* `native()`.

**7** *Remarks:* All memory allocation, including for the return value, shall be performed by `a`. Conversion, if any, is specified by [[fs.path.cvt]](fs.path.cvt.md).

```cpp
std::string string() const;
std::wstring wstring() const;
std::u8string u8string() const;
std::u16string u16string() const;
std::u32string u32string() const;
```

**8** *Returns:* `native()`.

**9** *Remarks:* Conversion, if any, is performed as specified by [[fs.path.cvt]](fs.path.cvt.md).
