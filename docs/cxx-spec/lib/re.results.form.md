# 30.10.5 Formatting [re.results.form]

[Contents](../README.md) · up: [30.10 Class template `match_results` \[re.results\]](re.results.md) · prev: [30.10.4 Element access \[re.results.acc\]](re.results.acc.md) · next: [30.10.6 Allocator \[re.results.all\]](re.results.all.md)

```cpp
template<class OutputIter>
  OutputIter format(
      OutputIter out,
      const char_type* fmt_first, const char_type* fmt_last,
      regex_constants::match_flag_type flags = regex_constants::format_default) const;
```

**1** *Preconditions:* `ready() == true` and `OutputIter` meets the requirements for a *Cpp17OutputIterator* ([[output.iterators]](output.iterators.md)).

**2** *Effects:* Copies the character sequence `[fmt_first, fmt_last)` to OutputIter `out`. Replaces each format specifier or escape sequence in the copied range with either the character(s) it represents or the sequence of characters within `*this` to which it refers. The bitmasks specified in `flags` determine which format specifiers and escape sequences are recognized.

**3** *Returns:* `out`.

```cpp
template<class OutputIter, class ST, class SA>
  OutputIter format(
      OutputIter out,
      const basic_string<char_type, ST, SA>& fmt,
      regex_constants::match_flag_type flags = regex_constants::format_default) const;
```

**4** *Effects:* Equivalent to:

```cpp
return format(out, fmt.data(), fmt.data() + fmt.size(), flags);
```

```cpp
template<class ST, class SA>
  basic_string<char_type, ST, SA> format(
      const basic_string<char_type, ST, SA>& fmt,
      regex_constants::match_flag_type flags = regex_constants::format_default) const;
```

**5** *Preconditions:* `ready() == true`.

**6** *Effects:* Constructs an empty string `result` of type `basic_string<char_type, ST, SA>` and calls:

```cpp
format(back_inserter(result), fmt, flags);
```

**7** *Returns:* `result`.

```cpp
string_type format(
    const char_type* fmt,
    regex_constants::match_flag_type flags = regex_constants::format_default) const;
```

**8** *Preconditions:* `ready() == true`.

**9** *Effects:* Constructs an empty string `result` of type `string_type` and calls:

```cpp
format(back_inserter(result), fmt, fmt + char_traits<char_type>::length(fmt), flags);
```

**10** *Returns:* `result`.
