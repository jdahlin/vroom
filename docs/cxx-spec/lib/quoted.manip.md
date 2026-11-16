# 29.7.8 Quoted manipulators [quoted.manip]

[Contents](../README.md) · up: [29.7 Formatting and manipulators \[iostream.format\]](iostream.format.md) · prev: [29.7.7 Extended manipulators \[ext.manip\]](ext.manip.md) · next: [29.8 String-based streams \[string.streams\]](string.streams.md)

**1** [*Note 1*: Quoted manipulators provide string insertion and extraction of quoted strings (for example, XML and CSV formats). Quoted manipulators are useful in ensuring that the content of a string with embedded spaces remains unchanged if inserted and then extracted via stream I/O. — *end note*]

```cpp
template<class charT>
  unspecified quoted(const charT* s, charT delim = charT('"'), charT escape = charT('\\'));
template<class charT, class traits, class Allocator>
  unspecified quoted(const basic_string<charT, traits, Allocator>& s,
                     charT delim = charT('"'), charT escape = charT('\\'));
template<class charT, class traits>
  unspecified quoted(basic_string_view<charT, traits> s,
                     charT delim = charT('"'), charT escape = charT('\\'));
```

**2** *Returns:* An object of unspecified type such that if `out` is an instance of `basic_ostream` with member type `char_type` the same as `charT` and with member type `traits_type`, which in the second and third forms is the same as `traits`, then the expression `out << quoted(s, delim, escape)` behaves as a formatted output function ([[ostream.formatted.reqmts]](ostream.formatted.reqmts.md)) of `out`. This forms a character sequence `seq`, initially consisting of the following elements:

- `delim`.
- Each character in `s`. If the character to be output is equal to `escape` or `delim`, as determined by `traits_type::eq`, first output `escape`.
- `delim`.

Let `x` be the number of elements initially in `seq`. Then padding is determined for `seq` as described in [[ostream.formatted.reqmts]](ostream.formatted.reqmts.md), `seq` is inserted as if by calling `out.rdbuf()->sputn(seq, n)`, where `n` is the larger of `out.width()` and `x`, and `out.width(0)` is called. The expression `out << quoted(s, delim, escape)` has type `basic_ostream<charT, traits>&` and value `out`.

```cpp
template<class charT, class traits, class Allocator>
  unspecified quoted(basic_string<charT, traits, Allocator>& s,
                     charT delim = charT('"'), charT escape = charT('\\'));
```

**3** *Returns:* An object of unspecified type such that:

- If `in` is an instance of `basic_istream` with member types `char_type` and `traits_type` the same as `charT` and `traits`, respectively, then the expression `in >> quoted(s, delim, escape)` behaves as if it extracts the following characters from `in` using `operator>>(basic_istream<charT, traits>&, charT&)` ([[istream.extractors]](istream.extractors.md)) which may throw `ios_base::failure` ([[ios.failure]](ios.failure.md)):

  - If the first character extracted is equal to `delim`, as determined by `traits_type::eq`, then:

    - Turn off the `skipws` flag.
    - `s.clear()`
    - Until an unescaped `delim` character is reached or `!in`, extract characters from `in` and append them to `s`, except that if an `escape` is reached, ignore it and append the next character to `s`.
    - Discard the final `delim` character.
    - Restore the `skipws` flag to its original value.

  - Otherwise, `in >> s`.

- If `out` is an instance of `basic_ostream` with member types `char_type` and `traits_type` the same as `charT` and `traits`, respectively, then the expression `out << quoted(s, delim, escape)` behaves as specified for the `const basic_string<charT, traits, Allocator>&` overload of the `quoted` function.

- The expression `in >> quoted(s, delim, escape)` has type `basic_istream<charT, traits>&` and value `in`.

- The expression `out << quoted(s, delim, escape)` has type `basic_ostream<charT, traits>&` and value `out`.
