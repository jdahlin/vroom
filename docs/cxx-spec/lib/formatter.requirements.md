# 20.20.5.1 Formatter requirements [formatter.requirements]

[Contents](../README.md) · up: [20.20.5 Formatter \[format.formatter\]](format.formatter.md) · prev: [20.20.5 Formatter \[format.formatter\]](format.formatter.md) · next: [20.20.5.2 Formatter specializations \[format.formatter.spec\]](format.formatter.spec.md)

**1** A type `F` meets the *Formatter* requirements if:

- it meets the

  - *Cpp17DefaultConstructible* ([Table 27](utility.arg.requirements.md#tab:cpp17.defaultconstructible)),
  - *Cpp17CopyConstructible* ([Table 29](utility.arg.requirements.md#tab:cpp17.copyconstructible)),
  - *Cpp17CopyAssignable* ([Table 31](utility.arg.requirements.md#tab:cpp17.copyassignable)), and
  - *Cpp17Destructible* ([Table 32](utility.arg.requirements.md#tab:cpp17.destructible))

  requirements,

- it is swappable ([[swappable.requirements]](swappable.requirements.md)) for lvalues, and

- the expressions shown in [Table 67](formatter.requirements.md#tab:formatter) are valid and have the indicated semantics.

**2** Given character type `charT`, output iterator type `Out`, and formatting argument type `T`, in [Table 67](formatter.requirements.md#tab:formatter):

- `f` is a value of type `F`,
- `u` is an lvalue of type `T`,
- `t` is a value of a type convertible to (possibly const) `T`,
- `PC` is `basic_format_parse_context<charT>`,
- `FC` is `basic_format_context<Out, charT>`,
- `pc` is an lvalue of type `PC`, and
- `fc` is an lvalue of type `FC`.

`pc.begin()` points to the beginning of the *format-spec* ([[format.string]](format.string.md)) of the replacement field being formatted in the format string. If *format-spec* is empty then either `pc.begin() == pc.end()` or `*pc.begin() == '}'`.

<a id="tab:formatter"></a>
**Table 67: *Formatter* requirements** [tab:formatter]

| Expression | Return type | Requirement |
| --- | --- | --- |
| `f.parse(pc)` | `PC::iterator` | Parses *format-spec* ([[format.string]](format.string.md)) for type `T` in the range `[pc.begin(), pc.end())` until the first unmatched character. Throws `format_error` unless the whole range is parsed or the unmatched character is `}`. [*Note 1*: This allows formatters to emit meaningful error messages. — *end note*] Stores the parsed format specifiers in `*this` and returns an iterator past the end of the parsed range. |
| `f.format(t, fc)` | `FC::iterator` | Formats `t` according to the specifiers stored in `*this`, writes the output to `fc.out()` and returns an iterator past the end of the output range. The output shall only depend on `t`, `fc.locale()`, and the range `[pc.begin(), pc.end())` from the last call to `f.parse(pc)`. |
| `f.format(u, fc)` | `FC::iterator` | As above, but does not modify `u`. |
