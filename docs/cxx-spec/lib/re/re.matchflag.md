# 30.5.2 Bitmask type `match_flag_type` [re.matchflag]

[Contents](../../README.md) · up: [30.5 Namespace `std::regex_constants` \[re.const\]](re.const.md) · prev: [30.5.1 Bitmask type `syntax_option_type` \[re.synopt\]](re.synopt.md) · next: [30.5.3 Implementation-defined `error_type` \[re.err\]](re.err.md)

```cpp
namespace std::regex_constants {
  using match_flag_type = T2;
  inline constexpr match_flag_type match_default = {};
  inline constexpr match_flag_type match_not_bol = unspecified;
  inline constexpr match_flag_type match_not_eol = unspecified;
  inline constexpr match_flag_type match_not_bow = unspecified;
  inline constexpr match_flag_type match_not_eow = unspecified;
  inline constexpr match_flag_type match_any = unspecified;
  inline constexpr match_flag_type match_not_null = unspecified;
  inline constexpr match_flag_type match_continuous = unspecified;
  inline constexpr match_flag_type match_prev_avail = unspecified;
  inline constexpr match_flag_type format_default = {};
  inline constexpr match_flag_type format_sed = unspecified;
  inline constexpr match_flag_type format_no_copy = unspecified;
  inline constexpr match_flag_type format_first_only = unspecified;
}
```

**1** The type `match_flag_type` is an implementation-defined bitmask type ([[bitmask.types]](../library/bitmask.types.md)). The constants of that type, except for `match_default` and `format_default`, are bitmask elements. The `match_default` and `format_default` constants are empty bitmasks. Matching a regular expression against a sequence of characters `[first, last)` proceeds according to the rules of the grammar specified for the regular expression object, modified according to the effects listed in [Table 137](re.matchflag.md#tab:re.matchflag) for any bitmask elements set.

<a id="tab:re.matchflag"></a>
**Table 137: `regex_constants::match_flag_type` effects when obtaining a match against a character container sequence `[first, last)`.** [tab:re.matchflag]

| Element | Effect(s) if set |
| --- | --- |
| `match_not_bol` | The first character in the sequence `[first, last)` shall be treated as though it is not at the beginning of a line, so the character `^` in the regular expression shall not match `[first, first)`. |
| `match_not_eol` | The last character in the sequence `[first, last)` shall be treated as though it is not at the end of a line, so the character `"$"` in the regular expression shall not match `[last, last)`. |
| `match_not_bow` | The expression `"\\b"` shall not match the sub-sequence `[first, first)`. |
| `match_not_eow` | The expression `"\\b"` shall not match the sub-sequence `[last, last)`. |
| `match_any` | If more than one match is possible then any match is an acceptable result. |
| `match_not_null` | The expression shall not match an empty sequence. |
| `match_continuous` | The expression shall only match a sub-sequence that begins at `first`. |
| `match_prev_avail` | `--first` is a valid iterator position. When this flag is set the flags `match_not_bol` and `match_not_bow` shall be ignored by the regular expression algorithms ([[re.alg]](re.alg.md)) and iterators ([[re.iter]](re.iter.md)). |
| `format_default` | When a regular expression match is to be replaced by a new string, the new string shall be constructed using the rules used by the ECMAScript replace function in ECMA-262, part 15.5.4.11 String.prototype.replace. In addition, during search and replace operations all non-overlapping occurrences of the regular expression shall be located and replaced, and sections of the input that did not match the expression shall be copied unchanged to the output string. |
| `format_sed` | When a regular expression match is to be replaced by a new string, the new string shall be constructed using the rules used by the sed utility in POSIX. |
| `format_no_copy` | During a search and replace operation, sections of the character container sequence being searched that do not match the regular expression shall not be copied to the output string. |
| `format_first_only` | When specified during a search and replace operation, only the first occurrence of the regular expression shall be replaced. |
