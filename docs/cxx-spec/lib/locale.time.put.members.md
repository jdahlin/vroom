# 28.4.5.3.1 Members [locale.time.put.members]

[Contents](../README.md) · up: [28.4.5.3 Class template `time_put` \[locale.time.put\]](locale.time.put.md) · prev: [28.4.5.3 Class template `time_put` \[locale.time.put\]](locale.time.put.md) · next: [28.4.5.3.2 Virtual functions \[locale.time.put.virtuals\]](locale.time.put.virtuals.md)

```cpp
iter_type put(iter_type s, ios_base& str, char_type fill, const tm* t,
              const charT* pattern, const charT* pat_end) const;
iter_type put(iter_type s, ios_base& str, char_type fill, const tm* t,
              char format, char modifier = 0) const;
```

**1** *Effects:* The first form steps through the sequence from `pattern` to `pat_end`, identifying characters that are part of a format sequence. Each character that is not part of a format sequence is written to `s` immediately, and each format sequence, as it is identified, results in a call to `do_put`; thus, format elements and other characters are interleaved in the output in the order in which they appear in the pattern. Format sequences are identified by converting each character `c` to a `char` value as if by `ct.narrow(c, 0)`, where `ct` is a reference to `ctype<charT>` obtained from `str.getloc()`. The first character of each sequence is equal to `'%'`, followed by an optional modifier character `mod`[^1] and a format specifier character `spec` as defined for the function `strftime`. If no modifier character is present, `mod` is zero. For each valid format sequence identified, calls `do_put(s, str, fill, t, spec, mod)`.

**2** The second form calls `do_put(s, str, fill, t, format, modifier)`.

**3** [*Note 1*: The `fill` argument may be used in the implementation-defined formats or by derivations. A space character is a reasonable default for this argument. — *end note*]

**4** *Returns:* An iterator pointing immediately after the last character produced.

[^1]: Although the C programming language defines no modifiers, most vendors do.
