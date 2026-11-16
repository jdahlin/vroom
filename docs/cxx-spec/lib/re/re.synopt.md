# 30.5.1 Bitmask type `syntax_option_type` [re.synopt]

[Contents](../../README.md) · up: [30.5 Namespace `std::regex_constants` \[re.const\]](re.const.md) · prev: [30.5 Namespace `std::regex_constants` \[re.const\]](re.const.md) · next: [30.5.2 Bitmask type `match_flag_type` \[re.matchflag\]](re.matchflag.md)

```cpp
namespace std::regex_constants {
  using syntax_option_type = T1;
  inline constexpr syntax_option_type icase = unspecified;
  inline constexpr syntax_option_type nosubs = unspecified;
  inline constexpr syntax_option_type optimize = unspecified;
  inline constexpr syntax_option_type collate = unspecified;
  inline constexpr syntax_option_type ECMAScript = unspecified;
  inline constexpr syntax_option_type basic = unspecified;
  inline constexpr syntax_option_type extended = unspecified;
  inline constexpr syntax_option_type awk = unspecified;
  inline constexpr syntax_option_type grep = unspecified;
  inline constexpr syntax_option_type egrep = unspecified;
  inline constexpr syntax_option_type multiline = unspecified;
}
```

**1** The type `syntax_option_type` is an implementation-defined bitmask type ([[bitmask.types]](../library/bitmask.types.md)). Setting its elements has the effects listed in [Table 136](re.synopt.md#tab:re.synopt). A valid value of type `syntax_option_type` shall have at most one of the grammar elements `ECMAScript`, `basic`, `extended`, `awk`, `grep`, `egrep`, set. If no grammar element is set, the default grammar is `ECMAScript`.

<a id="tab:re.synopt"></a>
**Table 136: `syntax_option_type` effects** [tab:re.synopt]

| Element | Effect(s) if set |
| --- | --- |
| `icase` | Specifies that matching of regular expressions against a character container sequence shall be performed without regard to case. |
| `nosubs` | Specifies that no sub-expressions shall be considered to be marked, so that when a regular expression is matched against a character container sequence, no sub-expression matches shall be stored in the supplied `match_results` object. |
| `optimize` | Specifies that the regular expression engine should pay more attention to the speed with which regular expressions are matched, and less to the speed with which regular expression objects are constructed. Otherwise it has no detectable effect on the program output. |
| `collate` | Specifies that character ranges of the form `"[a-b]"` shall be locale sensitive. |
| `ECMAScript` | Specifies that the grammar recognized by the regular expression engine shall be that used by ECMAScript in ECMA-262, as modified in [[re.grammar]](re.grammar.md). <br>*See also:* ECMA-262 15.10 |
| `basic` | Specifies that the grammar recognized by the regular expression engine shall be that used by basic regular expressions in POSIX. <br>*See also:* POSIX, Base Definitions and Headers, Section 9.3 |
| `extended` | Specifies that the grammar recognized by the regular expression engine shall be that used by extended regular expressions in POSIX. <br>*See also:* POSIX, Base Definitions and Headers, Section 9.4 |
| `awk` | Specifies that the grammar recognized by the regular expression engine shall be that used by the utility awk in POSIX. |
| `grep` | Specifies that the grammar recognized by the regular expression engine shall be that used by the utility grep in POSIX. |
| `egrep` | Specifies that the grammar recognized by the regular expression engine shall be that used by the utility grep when given the -E option in POSIX. |
| `multiline` | Specifies that `^` shall match the beginning of a line and `$` shall match the end of a line, if the `ECMAScript` engine is selected. |
