# 20.20.2.1 In general [format.string.general]

[Contents](../../README.md) · up: [20.20.2 Format string \[format.string\]](format.string.md) · prev: [20.20.2 Format string \[format.string\]](format.string.md) · next: [20.20.2.2 Standard format specifiers \[format.string.std\]](format.string.std.md)

**1** A *format string* for arguments `args` is a (possibly empty) sequence of *replacement fields*, *escape sequences*, and characters other than `{` and `}`. Let `charT` be the character type of the format string. Each character that is not part of a replacement field or an escape sequence is copied unchanged to the output. An escape sequence is one of `{{` or `}}`. It is replaced with `{` or `}`, respectively, in the output. The syntax of replacement fields is as follows:

```bnf
replacement-field:
    '{' arg-idₒₚₜ format-specifierₒₚₜ '}'
```

```bnf
arg-id:
    '0'
    positive-integer
```

```bnf
positive-integer:
    nonzero-digit
    positive-integer digit
```

```bnf
nonnegative-integer:
    digit
    nonnegative-integer digit
```

```bnf
nonzero-digit: one of
    '1 2 3 4 5 6 7 8 9'
```

```bnf
digit: one of
    '0 1 2 3 4 5 6 7 8 9'
```

```bnf
format-specifier:
    ':' format-spec
```

```bnf
format-spec:
    as specified by the 'formatter' specialization for the argument type
```

**2** The *arg-id* field specifies the index of the argument in `args` whose value is to be formatted and inserted into the output instead of the replacement field. If there is no argument with the index *arg-id* in `args`, the string is not a format string for `args`. The optional *format-specifier* field explicitly specifies a format for the replacement value.

**3** [*Example 1*:

```cpp
string s = format("{0}-{{", 8);         // value of s is "8-{"
```

— *end example*]

**4** If all *arg-id*s in a format string are omitted (including those in the *format-spec*, as interpreted by the corresponding `formatter` specialization), argument indices 0, 1, 2, … will automatically be used in that order. If some *arg-id*s are omitted and some are present, the string is not a format string. [*Note 1*: A format string cannot contain a mixture of automatic and manual indexing. — *end note*] [*Example 2*:

```cpp
string s0 = format("{} to {}",   "a", "b"); // OK, automatic indexing
string s1 = format("{1} to {0}", "a", "b"); // OK, manual indexing
string s2 = format("{0} to {}",  "a", "b"); // not a format string (mixing automatic and manual indexing),
                                            // throws format_error
string s3 = format("{} to {1}",  "a", "b"); // not a format string (mixing automatic and manual indexing),
                                            // throws format_error
```

— *end example*]

**5** The *format-spec* field contains *format specifications* that define how the value should be presented. Each type can define its own interpretation of the *format-spec* field. If *format-spec* does not conform to the format specifications for the argument type referred to by *arg-id*, the string is not a format string for `args`. [*Example 3*:

- For arithmetic, pointer, and string types the *format-spec* is interpreted as a *std-format-spec* as described in ([[format.string.std]](format.string.std.md)).
- For chrono types the *format-spec* is interpreted as a *chrono-format-spec* as described in ([[time.format]](../time/time.format.md)).
- For user-defined `formatter` specializations, the behavior of the `parse` member function determines how the *format-spec* is interpreted.

— *end example*]
