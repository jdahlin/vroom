# 27.13 Parsing [time.parse]

[Contents](../../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27.12 Formatting \[time.format\]](time.format.md) · next: [27.14 Header `<ctime>` synopsis \[ctime.syn\]](ctime.syn.md)

**1** Each `parse` overload specified in this subclause calls `from_stream` unqualified, so as to enable argument dependent lookup ([[basic.lookup.argdep]](../../basic/basic.lookup.argdep.md)). In the following paragraphs, let `is` denote an object of type `basic_istream<charT, traits>` and let `I` be `basic_istream<charT, traits>&`, where `charT` and `traits` are template parameters in that context.

```cpp
template<class charT, class traits, class Alloc, class Parsable>
  unspecified
    parse(const basic_string<charT, traits, Alloc>& fmt, Parsable& tp);
```

**2** *Constraints:* The expression

```cpp
from_stream(declval<basic_istream<charT, traits>&>(), fmt.c_str(), tp)
```

is well-formed when treated as an unevaluated operand.

**3** *Returns:* A manipulator such that the expression `is >> parse(fmt, tp)` has type `I`, has value `is`, and calls `from_stream(is, fmt.c_str(), tp)`.

```cpp
template<class charT, class traits, class Alloc, class Parsable>
  unspecified
    parse(const basic_string<charT, traits, Alloc>& fmt, Parsable& tp,
          basic_string<charT, traits, Alloc>& abbrev);
```

**4** *Constraints:* The expression

```cpp
from_stream(declval<basic_istream<charT, traits>&>(), fmt.c_str(), tp, addressof(abbrev))
```

is well-formed when treated as an unevaluated operand.

**5** *Returns:* A manipulator such that the expression `is >> parse(fmt, tp, abbrev)` has type `I`, has value `is`, and calls `from_stream(is, fmt.c_str(), tp, addressof(abbrev))`.

```cpp
template<class charT, class traits, class Alloc, class Parsable>
  unspecified
    parse(const basic_string<charT, traits, Alloc>& fmt, Parsable& tp,
          minutes& offset);
```

**6** *Constraints:* The expression

```cpp
from_stream(declval<basic_istream<charT, traits>&>(),
            fmt.c_str(), tp,
            declval<basic_string<charT, traits, Alloc>*>(),
            &offset)
```

is well-formed when treated as an unevaluated operand.

**7** *Returns:* A manipulator such that the expression `is >> parse(fmt, tp, offset)` has type `I`, has value `is`, and calls:

```cpp
from_stream(is,
            fmt.c_str(), tp,
            static_cast<basic_string<charT, traits, Alloc>*>(nullptr),
            &offset)
```

```cpp
template<class charT, class traits, class Alloc, class Parsable>
  unspecified
    parse(const basic_string<charT, traits, Alloc>& fmt, Parsable& tp,
          basic_string<charT, traits, Alloc>& abbrev, minutes& offset);
```

**8** *Constraints:* The expression

```cpp
from_stream(declval<basic_istream<charT, traits>&>(),
            fmt.c_str(), tp, addressof(abbrev), &offset)
```

is well-formed when treated as an unevaluated operand.

**9** *Returns:* A manipulator such that the expression `is >> parse(fmt, tp, abbrev, offset)` has type `I`, has value `is`, and calls `from_stream(is, fmt.c_str(), tp, addressof(abbrev), &offset)`.

**10** All `from_stream` overloads behave as unformatted input functions, except that they have an unspecified effect on the value returned by subsequent calls to `basic_istream<>::gcount()`. Each overload takes a format string containing ordinary characters and flags which have special meaning. Each flag begins with a `%`. Some flags can be modified by `E` or `O`. During parsing each flag interprets characters as parts of date and time types according to [Table 100](time.parse.md#tab:time.parse.spec). Some flags can be modified by a width parameter given as a positive decimal integer called out as `N` below which governs how many characters are parsed from the stream in interpreting the flag. All characters in the format string that are not represented in [Table 100](time.parse.md#tab:time.parse.spec), except for white space, are parsed unchanged from the stream. A white space character matches zero or more white space characters in the input stream.

**11** If the type being parsed cannot represent the information that the format flag refers to, `is.setstate(ios_base::failbit)` is called. [*Example 1*: A `duration` cannot represent a `weekday`. — *end example*] However, if a flag refers to a “time of day” (e.g. `%H`, `%I`, `%p`, etc.), then a specialization of `duration` is parsed as the time of day elapsed since midnight.

**12** If the `from_stream` overload fails to parse everything specified by the format string, or if insufficient information is parsed to specify a complete duration, time point, or calendrical data structure, `setstate(ios_base::failbit)` is called on the `basic_istream`.

<a id="tab:time.parse.spec"></a>
**Table 100: Meaning of `parse` flags** [tab:time.parse.spec]

| Flag | Parsed value |
| --- | --- |
| `%a` | The locale’s full or abbreviated case-insensitive weekday name. |
| `%A` | Equivalent to `%a`. |
| `%b` | The locale’s full or abbreviated case-insensitive month name. |
| `%B` | Equivalent to `%b`. |
| `%c` | The locale’s date and time representation. The modified command `%Ec` interprets the locale’s alternate date and time representation. |
| `%C` | The century as a decimal number. The modified command `%NC` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. The modified command `%EC` interprets the locale’s alternative representation of the century. |
| `%d` | The day of the month as a decimal number. The modified command `%Nd` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. The modified command `%Od` interprets the locale’s alternative representation of the day of the month. |
| `%D` | Equivalent to `%m/%d/%y`. |
| `%e` | Equivalent to `%d` and can be modified like `%d`. |
| `%F` | Equivalent to `%Y-%m-%d`. If modified with a width `N`, the width is applied to only `%Y`. |
| `%g` | The last two decimal digits of the ISO week-based year. The modified command `%Ng` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. |
| `%G` | The ISO week-based year as a decimal number. The modified command `%NG` specifies the maximum number of characters to read. If `N` is not specified, the default is 4. Leading zeroes are permitted but not required. |
| `%h` | Equivalent to `%b`. |
| `%H` | The hour (24-hour clock) as a decimal number. The modified command `%NH` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. The modified command `%OH` interprets the locale’s alternative representation. |
| `%I` | The hour (12-hour clock) as a decimal number. The modified command `%NI` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. The modified command `%OI` interprets the locale’s alternative representation. |
| `%j` | If the type being parsed is a specialization of `duration`, a decimal number of `days`. Otherwise, the day of the year as a decimal number. Jan 1 is `1`. In either case, the modified command `%Nj` specifies the maximum number of characters to read. If `N` is not specified, the default is 3. Leading zeroes are permitted but not required. |
| `%m` | The month as a decimal number. Jan is `1`. The modified command `%Nm` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. The modified command `%Om` interprets the locale’s alternative representation. |
| `%M` | The minutes as a decimal number. The modified command `%NM` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. The modified command `%OM` interprets the locale’s alternative representation. |
| `%n` | Matches one white space character. [*Note 1*: `%n`, `%t`, and a space can be combined to match a wide range of white-space patterns. For example, `"%n "` matches one or more white space characters, and `"%n%t%t"` matches one to three white space characters. — *end note*] |
| `%p` | The locale’s equivalent of the AM/PM designations associated with a 12-hour clock. |
| `%r` | The locale’s 12-hour clock time. |
| `%R` | Equivalent to `%H:%M`. |
| `%S` | The seconds as a decimal number. The modified command `%NS` specifies the maximum number of characters to read. If `N` is not specified, the default is 2 if the input time has a precision convertible to seconds. Otherwise the default width is determined by the decimal precision of the input and the field is interpreted as a `long double` in a fixed format. If encountered, the locale determines the decimal point character. Leading zeroes are permitted but not required. The modified command `%OS` interprets the locale’s alternative representation. |
| `%t` | Matches zero or one white space characters. |
| `%T` | Equivalent to `%H:%M:%S`. |
| `%u` | The ISO weekday as a decimal number (`1`-`7`), where Monday is `1`. The modified command `%Nu` specifies the maximum number of characters to read. If `N` is not specified, the default is `1`. Leading zeroes are permitted but not required. |
| `%U` | The week number of the year as a decimal number. The first Sunday of the year is the first day of week `01`. Days of the same year prior to that are in week `00`. The modified command `%NU` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. The modified command `%OU` interprets the locale’s alternative representation. |
| `%V` | The ISO week-based week number as a decimal number. The modified command `%NV` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. |
| `%w` | The weekday as a decimal number (`0`-`6`), where Sunday is `0`. The modified command `%Nw` specifies the maximum number of characters to read. If `N` is not specified, the default is `1`. Leading zeroes are permitted but not required. The modified command `%Ow` interprets the locale’s alternative representation. |
| `%W` | The week number of the year as a decimal number. The first Monday of the year is the first day of week `01`. Days of the same year prior to that are in week `00`. The modified command `%NW` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. The modified command `%OW` interprets the locale’s alternative representation. |
| `%x` | The locale’s date representation. The modified command `%Ex` interprets the locale’s alternate date representation. |
| `%X` | The locale’s time representation. The modified command `%EX` interprets the locale’s alternate time representation. |
| `%y` | The last two decimal digits of the year. If the century is not otherwise specified (e.g. with `%C`), values in the range `[69, 99]` are presumed to refer to the years 1969 to 1999, and values in the range `[00, 68]` are presumed to refer to the years 2000 to 2068. The modified command `%Ny` specifies the maximum number of characters to read. If `N` is not specified, the default is 2. Leading zeroes are permitted but not required. The modified commands `%Ey` and `%Oy` interpret the locale’s alternative representation. |
| `%Y` | The year as a decimal number. The modified command `%NY` specifies the maximum number of characters to read. If `N` is not specified, the default is 4. Leading zeroes are permitted but not required. The modified command `%EY` interprets the locale’s alternative representation. |
| `%z` | The offset from UTC in the format `[+\|-]hh[mm]`. For example `-0430` refers to 4 hours 30 minutes behind UTC, and `04` refers to 4 hours ahead of UTC. The modified commands `%Ez` and `%Oz` parse a `:` between the hours and minutes and render leading zeroes on the hour field optional: `[+\|-]h[h][:mm]`. For example `-04:30` refers to 4 hours 30 minutes behind UTC, and `4` refers to 4 hours ahead of UTC. |
| `%Z` | The time zone abbreviation or name. A single word is parsed. This word can only contain characters from the basic source character set ([[lex.charset]](../../lex/lex.charset.md)) that are alphanumeric, or one of `'_'`, `'/'`, `'-'`, or `'+'`. |
| `%%` | A `%` character is extracted. |
