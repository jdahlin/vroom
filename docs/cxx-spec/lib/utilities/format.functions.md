# 20.20.4 Formatting functions [format.functions]

[Contents](../../README.md) · up: [20.20 Formatting \[format\]](format.md) · prev: [20.20.3 Error reporting \[format.err.report\]](format.err.report.md) · next: [20.20.5 Formatter \[format.formatter\]](format.formatter.md)

**1** In the description of the functions, operator `+` is used for some of the iterator categories for which it does not have to be defined. In these cases the semantics of `a + n` are the same as in [[algorithms.requirements]](../algorithms/algorithms.requirements.md).

```cpp
template<class... Args>
  string format(string_view fmt, const Args&... args);
```

**2** *Effects:* Equivalent to:

```cpp
return vformat(fmt, make_format_args(args...));
```

```cpp
template<class... Args>
  wstring format(wstring_view fmt, const Args&... args);
```

**3** *Effects:* Equivalent to:

```cpp
return vformat(fmt, make_wformat_args(args...));
```

```cpp
template<class... Args>
  string format(const locale& loc, string_view fmt, const Args&... args);
```

**4** *Effects:* Equivalent to:

```cpp
return vformat(loc, fmt, make_format_args(args...));
```

```cpp
template<class... Args>
  wstring format(const locale& loc, wstring_view fmt, const Args&... args);
```

**5** *Effects:* Equivalent to:

```cpp
return vformat(loc, fmt, make_wformat_args(args...));
```

```cpp
string vformat(string_view fmt, format_args args);
wstring vformat(wstring_view fmt, wformat_args args);
string vformat(const locale& loc, string_view fmt, format_args args);
wstring vformat(const locale& loc, wstring_view fmt, wformat_args args);
```

**6** *Returns:* A string object holding the character representation of formatting arguments provided by `args` formatted according to specifications given in `fmt`. If present, `loc` is used for locale-specific formatting.

**7** *Throws:* As specified in [[format.err.report]](format.err.report.md).

```cpp
template<class Out, class... Args>
  Out format_to(Out out, string_view fmt, const Args&... args);
template<class Out, class... Args>
  Out format_to(Out out, wstring_view fmt, const Args&... args);
```

**8** *Effects:* Equivalent to:

```cpp
using context = basic_format_context<Out, decltype(fmt)::value_type>;
return vformat_to(out, fmt, make_format_args<context>(args...));
```

```cpp
template<class Out, class... Args>
  Out format_to(Out out, const locale& loc, string_view fmt, const Args&... args);
template<class Out, class... Args>
  Out format_to(Out out, const locale& loc, wstring_view fmt, const Args&... args);
```

**9** *Effects:* Equivalent to:

```cpp
using context = basic_format_context<Out, decltype(fmt)::value_type>;
return vformat_to(out, loc, fmt, make_format_args<context>(args...));
```

```cpp
template<class Out>
  Out vformat_to(Out out, string_view fmt,
                 format_args_t<type_identity_t<Out>, char> args);
template<class Out>
  Out vformat_to(Out out, wstring_view fmt,
                 format_args_t<type_identity_t<Out>, wchar_t> args);
template<class Out>
  Out vformat_to(Out out, const locale& loc, string_view fmt,
                 format_args_t<type_identity_t<Out>, char> args);
template<class Out>
  Out vformat_to(Out out, const locale& loc, wstring_view fmt,
                 format_args_t<type_identity_t<Out>, wchar_t> args);
```

**10** Let `charT` be `decltype(fmt)::value_type`.

**11** *Constraints:* `Out` satisfies `output_iterator<const charT&>`.

**12** *Preconditions:* `Out` models `output_iterator<const charT&>`.

**13** *Effects:* Places the character representation of formatting the arguments provided by `args`, formatted according to the specifications given in `fmt`, into the range `[out, out + N)`, where `N` is `formatted_size(fmt, args...)` for the functions without a `loc` parameter and `formatted_size(loc, fmt, args...)` for the functions with a `loc` parameter. If present, `loc` is used for locale-specific formatting.

**14** *Returns:* `out + N`.

**15** *Throws:* As specified in [[format.err.report]](format.err.report.md).

```cpp
template<class Out, class... Args>
  format_to_n_result<Out> format_to_n(Out out, iter_difference_t<Out> n,
                                      string_view fmt, const Args&... args);
template<class Out, class... Args>
  format_to_n_result<Out> format_to_n(Out out, iter_difference_t<Out> n,
                                      wstring_view fmt, const Args&... args);
template<class Out, class... Args>
  format_to_n_result<Out> format_to_n(Out out, iter_difference_t<Out> n,
                                      const locale& loc, string_view fmt,
                                      const Args&... args);
template<class Out, class... Args>
  format_to_n_result<Out> format_to_n(Out out, iter_difference_t<Out> n,
                                      const locale& loc, wstring_view fmt,
                                      const Args&... args);
```

**16** Let

- `charT` be `decltype(fmt)::value_type`,
- `N` be `formatted_size(fmt, args...)` for the functions without a `loc` parameter and `formatted_size(loc, fmt, args...)` for the functions with a `loc` parameter, and
- `M` be `clamp(n, 0, N)`.

**17** *Constraints:* `Out` satisfies `output_iterator<const charT&>`.

**18** *Preconditions:* `Out` models `output_iterator<const charT&>`, and `formatter<``T`<sub>*i*</sub>`, charT>` meets the *Formatter* requirements ([[formatter.requirements]](formatter.requirements.md)) for each `T`<sub>*i*</sub> in `Args`.

**19** *Effects:* Places the first `M` characters of the character representation of formatting the arguments provided by `args`, formatted according to the specifications given in `fmt`, into the range `[out, out + M)`. If present, `loc` is used for locale-specific formatting.

**20** *Returns:* `{out + M, N}`.

**21** *Throws:* As specified in [[format.err.report]](format.err.report.md).

```cpp
template<class... Args>
  size_t formatted_size(string_view fmt, const Args&... args);
template<class... Args>
  size_t formatted_size(wstring_view fmt, const Args&... args);
template<class... Args>
  size_t formatted_size(const locale& loc, string_view fmt, const Args&... args);
template<class... Args>
  size_t formatted_size(const locale& loc, wstring_view fmt, const Args&... args);
```

**22** Let `charT` be `decltype(fmt)::value_type`.

**23** *Preconditions:* `formatter<``T`<sub>*i*</sub>`, charT>` meets the *Formatter* requirements ([[formatter.requirements]](formatter.requirements.md)) for each `T`<sub>*i*</sub> in `Args`.

**24** *Returns:* The number of characters in the character representation of formatting arguments `args` formatted according to specifications given in `fmt`. If present, `loc` is used for locale-specific formatting.

**25** *Throws:* As specified in [[format.err.report]](format.err.report.md).
