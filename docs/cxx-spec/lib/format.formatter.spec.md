# 20.20.5.2 Formatter specializations [format.formatter.spec]

[Contents](../README.md) · up: [20.20.5 Formatter \[format.formatter\]](format.formatter.md) · prev: [20.20.5.1 Formatter requirements \[formatter.requirements\]](formatter.requirements.md) · next: [20.20.5.3 Class template `basic_format_parse_context` \[format.parse.ctx\]](format.parse.ctx.md)

**1** The functions defined in [[format.functions]](format.functions.md) use specializations of the class template `formatter` to format individual arguments.

**2** Let `charT` be either `char` or `wchar_t`. Each specialization of `formatter` is either enabled or disabled, as described below. [*Note 1*: Enabled specializations meet the *Formatter* requirements, and disabled specializations do not. — *end note*] Each header that declares the template `formatter` provides the following enabled specializations:

- The specializations

  ```cpp
  template<> struct formatter<char, char>;
  template<> struct formatter<char, wchar_t>;
  template<> struct formatter<wchar_t, wchar_t>;
  ```

- For each `charT`, the string type specializations

  ```cpp
  template<> struct formatter<charT*, charT>;
  template<> struct formatter<const charT*, charT>;
  template<size_t N> struct formatter<const charT[N], charT>;
  template<class traits, class Allocator>
    struct formatter<basic_string<charT, traits, Allocator>, charT>;
  template<class traits>
    struct formatter<basic_string_view<charT, traits>, charT>;
  ```

- For each `charT`, for each cv-unqualified arithmetic type `ArithmeticT` other than `char`, `wchar_t`, `char8_t`, `char16_t`, or `char32_t`, a specialization

  ```cpp
  template<> struct formatter<ArithmeticT, charT>;
  ```

- For each `charT`, the pointer type specializations

  ```cpp
  template<> struct formatter<nullptr_t, charT>;
  template<> struct formatter<void*, charT>;
  template<> struct formatter<const void*, charT>;
  ```

The `parse` member functions of these formatters interpret the format specification as a *std-format-spec* as described in [[format.string.std]](format.string.std.md). [*Note 2*: Specializations such as `formatter<wchar_t, char>` and `formatter<const char*, wchar_t>` that would require implicit multibyte / wide string or character conversion are disabled. — *end note*]

**3** For any types `T` and `charT` for which neither the library nor the user provides an explicit or partial specialization of the class template `formatter`, `formatter<T, charT>` is disabled.

**4** If the library provides an explicit or partial specialization of `formatter<T, charT>`, that specialization is enabled except as noted otherwise.

**5** If `F` is a disabled specialization of `formatter`, these values are `false`:

- `is_default_constructible_v<F>`,
- `is_copy_constructible_v<F>`,
- `is_move_constructible_v<F>`,
- `is_copy_assignable_v<F>`, and
- `is_move_assignable_v<F>`.

**6** An enabled specialization `formatter<T, charT>` meets the *Formatter* requirements ([[formatter.requirements]](formatter.requirements.md)). [*Example 1*:

```cpp
#include <format>

enum color { red, green, blue };
const char* color_names[] = { "red", "green", "blue" };

template<> struct std::formatter<color> : std::formatter<const char*> {
  auto format(color c, format_context& ctx) {
    return formatter<const char*>::format(color_names[c], ctx);
  }
};

struct err {};

std::string s0 = std::format("{}", 42);         // OK, library-provided formatter
std::string s1 = std::format("{}", L"foo");     // error: disabled formatter
std::string s2 = std::format("{}", red);        // OK, user-provided formatter
std::string s3 = std::format("{}", err{});      // error: disabled formatter
```

— *end example*]
