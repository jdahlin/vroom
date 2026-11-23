# 28.4.5.1.1 Members [locale.time.get.members]

[Contents](../../README.md) · up: [28.4.5.1 Class template `time_get` \[locale.time.get\]](locale.time.get.md) · prev: [28.4.5.1 Class template `time_get` \[locale.time.get\]](locale.time.get.md) · next: [28.4.5.1.2 Virtual functions \[locale.time.get.virtuals\]](locale.time.get.virtuals.md)

```cpp
dateorder date_order() const;
```

**1** *Returns:* `do_date_order()`.

```cpp
iter_type get_time(iter_type s, iter_type end, ios_base& str,
                   ios_base::iostate& err, tm* t) const;
```

**2** *Returns:* `do_get_time(s, end, str, err, t)`.

```cpp
iter_type get_date(iter_type s, iter_type end, ios_base& str,
                   ios_base::iostate& err, tm* t) const;
```

**3** *Returns:* `do_get_date(s, end, str, err, t)`.

```cpp
iter_type get_weekday(iter_type s, iter_type end, ios_base& str,
                      ios_base::iostate& err, tm* t) const;
iter_type get_monthname(iter_type s, iter_type end, ios_base& str,
                        ios_base::iostate& err, tm* t) const;
```

**4** *Returns:* `do_get_weekday(s, end, str, err, t)` or `do_get_monthname(s, end, str, err, t)`.

```cpp
iter_type get_year(iter_type s, iter_type end, ios_base& str,
                   ios_base::iostate& err, tm* t) const;
```

**5** *Returns:* `do_get_year(s, end, str, err, t)`.

```cpp
iter_type get(iter_type s, iter_type end, ios_base& f, ios_base::iostate& err,
              tm* t, char format, char modifier = 0) const;
```

**6** *Returns:* `do_get(s, end, f, err, t, format, modifier)`.

```cpp
iter_type get(iter_type s, iter_type end, ios_base& f, ios_base::iostate& err,
              tm* t, const char_type* fmt, const char_type* fmtend) const;
```

**7** *Preconditions:* `[fmt, fmtend)` is a valid range.

**8** *Effects:* The function starts by evaluating `err = ios_base::goodbit`. It then enters a loop, reading zero or more characters from `s` at each iteration. Unless otherwise specified below, the loop terminates when the first of the following conditions holds:

- The expression `fmt == fmtend` evaluates to `true`.
- The expression `err == ios_base::goodbit` evaluates to `false`.
- The expression `s == end` evaluates to `true`, in which case the function evaluates `err = ios_base::eofbit | ios_base::failbit`.
- The next element of `fmt` is equal to `'%'`, optionally followed by a modifier character, followed by a conversion specifier character, `format`, together forming a conversion specification valid for the ISO/IEC 9945 function `strptime`. If the number of elements in the range `[fmt, fmtend)` is not sufficient to unambiguously determine whether the conversion specification is complete and valid, the function evaluates `err = ios_base::failbit`. Otherwise, the function evaluates `s = do_get(s, end, f, err, t, format, modifier)`, where the value of `modifier` is `'\0'` when the optional modifier is absent from the conversion specification. If `err == ios_base::goodbit` holds after the evaluation of the expression, the function increments `fmt` to point just past the end of the conversion specification and continues looping.
- The expression `isspace(*fmt, f.getloc())` evaluates to `true`, in which case the function first increments `fmt` until `fmt == fmtend || !isspace(*fmt, f.getloc())` evaluates to `true`, then advances `s` until `s == end || !isspace(*s, f.getloc())` is `true`, and finally resumes looping.
- The next character read from `s` matches the element pointed to by `fmt` in a case-insensitive comparison, in which case the function evaluates `++fmt, ++s` and continues looping. Otherwise, the function evaluates `err = ios_base::failbit`.

**9** [*Note 1*: The function uses the `ctype<charT>` facet installed in `f`’s locale to determine valid whitespace characters. It is unspecified by what means the function performs case-insensitive comparison or whether multi-character sequences are considered while doing so. — *end note*]

**10** *Returns:* `s`.
