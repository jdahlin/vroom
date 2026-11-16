# 30.5.3 Implementation-defined `error_type` [re.err]

[Contents](../../README.md) · up: [30.5 Namespace `std::regex_constants` \[re.const\]](re.const.md) · prev: [30.5.2 Bitmask type `match_flag_type` \[re.matchflag\]](re.matchflag.md) · next: [30.6 Class `regex_error` \[re.badexp\]](re.badexp.md)

```cpp
namespace std::regex_constants {
  using error_type = T3;
  inline constexpr error_type error_collate = unspecified;
  inline constexpr error_type error_ctype = unspecified;
  inline constexpr error_type error_escape = unspecified;
  inline constexpr error_type error_backref = unspecified;
  inline constexpr error_type error_brack = unspecified;
  inline constexpr error_type error_paren = unspecified;
  inline constexpr error_type error_brace = unspecified;
  inline constexpr error_type error_badbrace = unspecified;
  inline constexpr error_type error_range = unspecified;
  inline constexpr error_type error_space = unspecified;
  inline constexpr error_type error_badrepeat = unspecified;
  inline constexpr error_type error_complexity = unspecified;
  inline constexpr error_type error_stack = unspecified;
}
```

**1** The type `error_type` is an implementation-defined enumerated type ([[enumerated.types]](../library/enumerated.types.md)). Values of type `error_type` represent the error conditions described in [Table 138](re.err.md#tab:re.err):

<a id="tab:re.err"></a>
**Table 138: `error_type` values in the C locale** [tab:re.err]

| Value | Error condition |
| --- | --- |
| `error_collate` | The expression contained an invalid collating element name. |
| `error_ctype` | The expression contained an invalid character class name. |
| `error_escape` | The expression contained an invalid escaped character, or a trailing escape. |
| `error_backref` | The expression contained an invalid back reference. |
| `error_brack` | The expression contained mismatched `[` and `]`. |
| `error_paren` | The expression contained mismatched `(` and `)`. |
| `error_brace` | The expression contained mismatched `{` and `}` |
| `error_badbrace` | The expression contained an invalid range in a `{}` expression. |
| `error_range` | The expression contained an invalid character range, such as `[b-a]` in most encodings. |
| `error_space` | There was insufficient memory to convert the expression into a finite state machine. |
| `error_badrepeat` | One of `*?+{` was not preceded by a valid regular expression. |
| `error_complexity` | The complexity of an attempted match against a regular expression exceeded a pre-set level. |
| `error_stack` | There was insufficient memory to determine whether the regular expression could match the specified character sequence. |
