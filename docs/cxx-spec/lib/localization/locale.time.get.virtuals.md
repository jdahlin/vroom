# 28.4.5.1.2 Virtual functions [locale.time.get.virtuals]

[Contents](../../README.md) · up: [28.4.5.1 Class template `time_get` \[locale.time.get\]](locale.time.get.md) · prev: [28.4.5.1.1 Members \[locale.time.get.members\]](locale.time.get.members.md) · next: [28.4.5.2 Class template `time_get_byname` \[locale.time.get.byname\]](locale.time.get.byname.md)

```cpp
dateorder do_date_order() const;
```

**1** *Returns:* An enumeration value indicating the preferred order of components for those date formats that are composed of day, month, and year.[^1] Returns `no_order` if the date format specified by `'x'` contains other variable components (e.g., Julian day, week number, week day).

```cpp
iter_type do_get_time(iter_type s, iter_type end, ios_base& str,
                      ios_base::iostate& err, tm* t) const;
```

**2** *Effects:* Reads characters starting at `s` until it has extracted those `struct tm` members, and remaining format characters, used by `time_put<>::put` to produce the format specified by `"%H:%M:%S"`, or until it encounters an error or end of sequence.

**3** *Returns:* An iterator pointing immediately beyond the last character recognized as possibly part of a valid time.

```cpp
iter_type do_get_date(iter_type s, iter_type end, ios_base& str,
                      ios_base::iostate& err, tm* t) const;
```

**4** *Effects:* Reads characters starting at `s` until it has extracted those `struct tm` members and remaining format characters used by `time_put<>::put` to produce one of the following formats, or until it encounters an error. The format depends on the value returned by `date_order()` as shown in [Table 113](locale.time.get.virtuals.md#tab:locale.time.get.dogetdate).

<a id="tab:locale.time.get.dogetdate"></a>
**Table 113: `do_get_date` effects** [tab:locale.time.get.dogetdate]

| `date_order()` | Format |
| --- | --- |
| `no_order` | `"%m%d%y"` |
| `dmy` | `"%d%m%y"` |
| `mdy` | `"%m%d%y"` |
| `ymd` | `"%y%m%d"` |
| `ydm` | `"%y%d%m"` |

**5** An implementation may also accept additional implementation-defined formats.

**6** *Returns:* An iterator pointing immediately beyond the last character recognized as possibly part of a valid date.

```cpp
iter_type do_get_weekday(iter_type s, iter_type end, ios_base& str,
                         ios_base::iostate& err, tm* t) const;
iter_type do_get_monthname(iter_type s, iter_type end, ios_base& str,
                           ios_base::iostate& err, tm* t) const;
```

**7** *Effects:* Reads characters starting at `s` until it has extracted the (perhaps abbreviated) name of a weekday or month. If it finds an abbreviation that is followed by characters that could match a full name, it continues reading until it matches the full name or fails. It sets the appropriate `struct tm` member accordingly.

**8** *Returns:* An iterator pointing immediately beyond the last character recognized as part of a valid name.

```cpp
iter_type do_get_year(iter_type s, iter_type end, ios_base& str,
                      ios_base::iostate& err, tm* t) const;
```

**9** *Effects:* Reads characters starting at `s` until it has extracted an unambiguous year identifier. It is implementation-defined whether two-digit year numbers are accepted, and (if so) what century they are assumed to lie in. Sets the `t->tm_year` member accordingly.

**10** *Returns:* An iterator pointing immediately beyond the last character recognized as part of a valid year identifier.

```cpp
iter_type do_get(iter_type s, iter_type end, ios_base& f,
                 ios_base::iostate& err, tm* t, char format, char modifier) const;
```

**11** *Preconditions:* `t` points to an object.

**12** *Effects:* The function starts by evaluating `err = ios_base::goodbit`. It then reads characters starting at `s` until it encounters an error, or until it has extracted and assigned those `struct tm` members, and any remaining format characters, corresponding to a conversion directive appropriate for the ISO/IEC 9945 function `strptime`, formed by concatenating `'%'`, the `modifier` character, when non-NUL, and the `format` character. When the concatenation fails to yield a complete valid directive the function leaves the object pointed to by `t` unchanged and evaluates `err |= ios_base::failbit`. When `s == end` evaluates to `true` after reading a character the function evaluates `err |= ios_base::eofbit`.

**13** For complex conversion directives such as `%c`, `%x`, or `%X`, or directives that involve the optional modifiers `E` or `O`, when the function is unable to unambiguously determine some or all `struct tm` members from the input sequence `[s, end)`, it evaluates `err |= ios_base::eofbit`. In such cases the values of those `struct tm` members are unspecified and may be outside their valid range.

**14** *Remarks:* It is unspecified whether multiple calls to `do_get()` with the address of the same `struct tm` object will update the current contents of the object or simply overwrite its members. Portable programs should zero out the object before invoking the function.

**15** *Returns:* An iterator pointing immediately beyond the last character recognized as possibly part of a valid input sequence for the given `format` and `modifier`.

[^1]: This function is intended as a convenience only, for common formats, and may return `no_order` in valid locales.
