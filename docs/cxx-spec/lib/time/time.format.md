# 27.12 Formatting [time.format]

[Contents](../../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27.11.9.3 Non-member functions \[time.zone.link.nonmembers\]](time.zone.link.nonmembers.md) · next: [27.13 Parsing \[time.parse\]](time.parse.md)

**1** Each `formatter` ([[format.formatter]](../utilities/format.formatter.md)) specialization in the chrono library ([[time.syn]](time.syn.md)) meets the *Formatter* requirements ([[formatter.requirements]](../utilities/formatter.requirements.md)). The `parse` member functions of these formatters interpret the format specification as a *chrono-format-spec* according to the following syntax:

```bnf
chrono-format-spec:
    fill-and-alignₒₚₜ widthₒₚₜ precisionₒₚₜ chrono-specsₒₚₜ
```

```bnf
chrono-specs:
    conversion-spec
    chrono-specs conversion-spec
    chrono-specs literal-char
```

```bnf
literal-char:
    any character other than '{', '}', or '%'
```

```bnf
conversion-spec:
    '%' modifierₒₚₜ type
```

```bnf
modifier: one of
    'E O'
```

```bnf
type: one of
    'a A b B c C d D e F g G h H I j m M n'
    'p q Q r R S t T u U V w W x X y Y z Z %'
```

The productions *fill-and-align*, *width*, and *precision* are described in [[format.string]](../utilities/format.string.md). Giving a *precision* specification in the *chrono-format-spec* is valid only for `std::chrono::duration` types where the representation type `Rep` is a floating-point type. For all other `Rep` types, an exception of type `format_error` is thrown if the *chrono-format-spec* contains a *precision* specification. All ordinary multibyte characters represented by *literal-char* are copied unchanged to the output.

**2** Each conversion specifier *conversion-spec* is replaced by appropriate characters as described in [Table 99](time.format.md#tab:time.format.spec). Some of the conversion specifiers depend on the locale that is passed to the formatting function if the latter takes one, or the global locale otherwise. If the formatted object does not contain the information the conversion specifier refers to, an exception of type `format_error` is thrown.

**3** The result of formatting a `std::chrono::duration` instance holding a negative value, or an `hh_mm_ss` object `h` for which `h.is_negative()` is `true`, is equivalent to the output of the corresponding positive value, with a `STATICALLY-WIDEN<charT>("-")` character sequence placed before the replacement of the initial conversion specifier. [*Example 1*:

```cpp
cout << format("{:%T}", -10'000s);          // prints: -02:46:40
cout << format("{:%H:%M:%S}", -10'000s);    // prints: -02:46:40
cout << format("minutes {:%M, hours %H, seconds %S}", -10'000s);
                                            // prints: minutes -46, hours 02, seconds 40
```

— *end example*]

**4** Unless explicitly requested, the result of formatting a chrono type does not contain time zone abbreviation and time zone offset information. If the information is available, the conversion specifiers `%Z` and `%z` will format this information (respectively). [*Note 1*: If the information is not available and a `%Z` or `%z` conversion specifier appears in the *chrono-format-spec*, an exception of type `format_error` is thrown, as described above. — *end note*]

**5** If the type being formatted does not contain the information that the format flag needs, an exception of type `format_error` is thrown. [*Example 2*: A `duration` does not contain enough information to format as a `weekday`. — *end example*] However, if a flag refers to a “time of day” (e.g. `%H`, `%I`, `%p`, etc.), then a specialization of `duration` is interpreted as the time of day elapsed since midnight.

<a id="tab:time.format.spec"></a>
**Table 99: Meaning of conversion specifiers** [tab:time.format.spec]

| Specifier | Replacement |
| --- | --- |
| `%a` | The locale’s abbreviated weekday name. If the value does not contain a valid weekday, an exception of type `format_error` is thrown. |
| `%A` | The locale’s full weekday name. If the value does not contain a valid weekday, an exception of type `format_error` is thrown. |
| `%b` | The locale’s abbreviated month name. If the value does not contain a valid month, an exception of type `format_error` is thrown. |
| `%B` | The locale’s full month name. If the value does not contain a valid month, an exception of type `format_error` is thrown. |
| `%c` | The locale’s date and time representation. The modified command `%Ec` produces the locale’s alternate date and time representation. |
| `%C` | The year divided by 100 using floored division. If the result is a single decimal digit, it is prefixed with `0`. The modified command `%EC` produces the locale’s alternative representation of the century. |
| `%d` | The day of month as a decimal number. If the result is a single decimal digit, it is prefixed with `0`. The modified command `%Od` produces the locale’s alternative representation. |
| `%D` | Equivalent to `%m/%d/%y`. |
| `%e` | The day of month as a decimal number. If the result is a single decimal digit, it is prefixed with a space. The modified command `%Oe` produces the locale’s alternative representation. |
| `%F` | Equivalent to `%Y-%m-%d`. |
| `%g` | The last two decimal digits of the ISO week-based year. If the result is a single digit it is prefixed by `0`. |
| `%G` | The ISO week-based year as a decimal number. If the result is less than four digits it is left-padded with `0` to four digits. |
| `%h` | Equivalent to `%b`. |
| `%H` | The hour (24-hour clock) as a decimal number. If the result is a single digit, it is prefixed with `0`. The modified command `%OH` produces the locale’s alternative representation. |
| `%I` | The hour (12-hour clock) as a decimal number. If the result is a single digit, it is prefixed with `0`. The modified command `%OI` produces the locale’s alternative representation. |
| `%j` | If the type being formatted is a specialization of `duration`, the decimal number of `days` without padding. Otherwise, the day of the year as a decimal number. Jan 1 is `001`. If the result is less than three digits, it is left-padded with `0` to three digits. |
| `%m` | The month as a decimal number. Jan is `01`. If the result is a single digit, it is prefixed with `0`. The modified command `%Om` produces the locale’s alternative representation. |
| `%M` | The minute as a decimal number. If the result is a single digit, it is prefixed with `0`. The modified command `%OM` produces the locale’s alternative representation. |
| `%n` | A new-line character. |
| `%p` | The locale’s equivalent of the AM/PM designations associated with a 12-hour clock. |
| `%q` | The duration’s unit suffix as specified in [[time.duration.io]](time.duration.io.md). |
| `%Q` | The duration’s numeric value (as if extracted via `.count()`). |
| `%r` | The locale’s 12-hour clock time. |
| `%R` | Equivalent to `%H:%M`. |
| `%S` | Seconds as a decimal number. If the number of seconds is less than `10`, the result is prefixed with `0`. If the precision of the input cannot be exactly represented with seconds, then the format is a decimal floating-point number with a fixed format and a precision matching that of the precision of the input (or to a microseconds precision if the conversion to floating-point decimal seconds cannot be made within 18 fractional digits). The character for the decimal point is localized according to the locale. The modified command `%OS` produces the locale’s alternative representation. |
| `%t` | A horizontal-tab character. |
| `%T` | Equivalent to `%H:%M:%S`. |
| `%u` | The ISO weekday as a decimal number (`1`-`7`), where Monday is `1`. The modified command `%Ou` produces the locale’s alternative representation. |
| `%U` | The week number of the year as a decimal number. The first Sunday of the year is the first day of week `01`. Days of the same year prior to that are in week `00`. If the result is a single digit, it is prefixed with `0`. The modified command `%OU` produces the locale’s alternative representation. |
| `%V` | The ISO week-based week number as a decimal number. If the result is a single digit, it is prefixed with `0`. The modified command `%OV` produces the locale’s alternative representation. |
| `%w` | The weekday as a decimal number (`0`-`6`), where Sunday is `0`. The modified command `%Ow` produces the locale’s alternative representation. |
| `%W` | The week number of the year as a decimal number. The first Monday of the year is the first day of week `01`. Days of the same year prior to that are in week `00`. If the result is a single digit, it is prefixed with `0`. The modified command `%OW` produces the locale’s alternative representation. |
| `%x` | The locale’s date representation. The modified command `%Ex` produces the locale’s alternate date representation. |
| `%X` | The locale’s time representation. The modified command `%EX` produces the locale’s alternate time representation. |
| `%y` | The last two decimal digits of the year. If the result is a single digit it is prefixed by `0`. The modified command `%Oy` produces the locale’s alternative representation. The modified command `%Ey` produces the locale’s alternative representation of offset from `%EC` (year only). |
| `%Y` | The year as a decimal number. If the result is less than four digits it is left-padded with `0` to four digits. The modified command `%EY` produces the locale’s alternative full year representation. |
| `%z` | The offset from UTC in the ISO 8601 format. For example `-0430` refers to 4 hours 30 minutes behind UTC. If the offset is zero, `+0000` is used. The modified commands `%Ez` and `%Oz` insert a `:` between the hours and minutes: `-04:30`. If the offset information is not available, an exception of type `format_error` is thrown. |
| `%Z` | The time zone abbreviation. If the time zone abbreviation is not available, an exception of type `format_error` is thrown. |
| `%%` | A `%` character. |

**6** If the *chrono-specs* is omitted, the chrono object is formatted as if by streaming it to `std::ostringstream os` and copying `os.str()` through the output iterator of the context with additional padding and adjustments as specified by the format specifiers. [*Example 3*:

```cpp
string s = format("{:=>8}", 42ms);      // value of s is "====42ms"
```

— *end example*]

```cpp
template<class Duration, class charT>
  struct formatter<chrono::sys_time<Duration>, charT>;
```

**7** *Remarks:* If `%Z` is used, it is replaced with `STATICALLY-WIDEN<charT>("UTC")`. If `%z` (or a modified variant of `%z`) is used, an offset of `0min` is formatted.

```cpp
template<class Duration, class charT>
  struct formatter<chrono::utc_time<Duration>, charT>;
```

**8** *Remarks:* If `%Z` is used, it is replaced with `STATICALLY-WIDEN<charT>("UTC")`. If `%z` (or a modified variant of `%z`) is used, an offset of `0min` is formatted. If the argument represents a time during a positive leap second insertion, and if a seconds field is formatted, the integral portion of that format is `STATICALLY-WIDEN<charT>("60")`.

```cpp
template<class Duration, class charT>
  struct formatter<chrono::tai_time<Duration>, charT>;
```

**9** *Remarks:* If `%Z` is used, it is replaced with `STATICALLY-WIDEN<charT>("TAI")`. If `%z` (or a modified variant of `%z`) is used, an offset of `0min` is formatted. The date and time formatted are equivalent to those formatted by a `sys_time` initialized with

```cpp
sys_time<Duration>{tp.time_since_epoch()} -
  (sys_days{1970y/January/1} - sys_days{1958y/January/1})
```

```cpp
template<class Duration, class charT>
  struct formatter<chrono::gps_time<Duration>, charT>;
```

**10** *Remarks:* If `%Z` is used, it is replaced with `STATICALLY-WIDEN<charT>("GPS")`. If `%z` (or a modified variant of `%z`) is used, an offset of `0min` is formatted. The date and time formatted are equivalent to those formatted by a `sys_time` initialized with

```cpp
sys_time<Duration>{tp.time_since_epoch()} +
  (sys_days{1980y/January/Sunday[1]} - sys_days{1970y/January/1})
```

```cpp
template<class Duration, class charT>
  struct formatter<chrono::file_time<Duration>, charT>;
```

**11** *Remarks:* If `%Z` is used, it is replaced with `STATICALLY-WIDEN<charT>("UTC")`. If `%z` (or a modified variant of `%z`) is used, an offset of `0min` is formatted. The date and time formatted are equivalent to those formatted by a `sys_time` initialized with `clock_cast<system_clock>(t)`, or by a `utc_time` initialized with `clock_cast<utc_clock>(t)`, where `t` is the first argument to `format`.

```cpp
template<class Duration, class charT>
  struct formatter<chrono::local_time<Duration>, charT>;
```

**12** *Remarks:* If `%Z`, `%z`, or a modified version of `%z` is used, an exception of type `format_error` is thrown.

```cpp
template<class Duration> struct local-time-format-t {           // exposition only
  local_time<Duration> time;                                    // exposition only
  const string* abbrev;                                         // exposition only
  const seconds* offset_sec;                                    // exposition only
};
```

```cpp
template<class Duration>
  local-time-format-t<Duration>
    local_time_format(local_time<Duration> time, const string* abbrev = nullptr,
                      const seconds* offset_sec = nullptr);
```

**13** *Returns:* `{time, abbrev, offset_sec}`.

```cpp
template<class Duration, class charT>
  struct formatter<chrono::local-time-format-t<Duration>, charT>;
```

**14** Let `f` be a `local-time-format-t<Duration>` object passed to `formatter::format`.

**15** *Remarks:* If `%Z` is used, it is replaced with `*f.abbrev` if `f.abbrev` is not a null pointer value. If `%Z` is used and `f.abbrev` is a null pointer value, an exception of type `format_error` is thrown. If `%z` (or a modified variant of `%z`) is used, it is formatted with the value of `*f.offset_sec` if `f.offset_sec` is not a null pointer value. If `%z` (or a modified variant of `%z`) is used and `f.offset_sec` is a null pointer value, then an exception of type `format_error` is thrown.

```cpp
template<class Duration, class TimeZonePtr, class charT>
struct formatter<chrono::zoned_time<Duration, TimeZonePtr>, charT>
    : formatter<chrono::local-time-format-t<Duration>, charT> {
  template<class FormatContext>
    typename FormatContext::iterator
      format(const chrono::zoned_time<Duration, TimeZonePtr>& tp, FormatContext& ctx);
};
```

```cpp
template<class FormatContext>
  typename FormatContext::iterator
    format(const chrono::zoned_time<Duration, TimeZonePtr>& tp, FormatContext& ctx);
```

**16** *Effects:* Equivalent to:

```cpp
sys_info info = tp.get_info();
return formatter<chrono::local-time-format-t<Duration>, charT>::
         format({tp.get_local_time(), &info.abbrev, &info.offset}, ctx);
```
