# 28.4.5.3.2 Virtual functions [locale.time.put.virtuals]

[Contents](../README.md) · up: [28.4.5.3 Class template `time_put` \[locale.time.put\]](locale.time.put.md) · prev: [28.4.5.3.1 Members \[locale.time.put.members\]](locale.time.put.members.md) · next: [28.4.5.4 Class template `time_put_byname` \[locale.time.put.byname\]](locale.time.put.byname.md)

```cpp
iter_type do_put(iter_type s, ios_base&, char_type fill, const tm* t,
                 char format, char modifier) const;
```

**1** *Effects:* Formats the contents of the parameter `t` into characters placed on the output sequence `s`. Formatting is controlled by the parameters `format` and `modifier`, interpreted identically as the format specifiers in the string argument to the standard library function `strftime()`[^1], except that the sequence of characters produced for those specifiers that are described as depending on the C locale are instead implementation-defined.[^2]

**2** *Returns:* An iterator pointing immediately after the last character produced. [*Note 1*: The `fill` argument may be used in the implementation-defined formats or by derivations. A space character is a reasonable default for this argument. — *end note*]

[^1]: Interpretation of the `modifier` argument is implementation-defined, but should follow POSIX conventions.
[^2]: Implementations should refer to other standards such as POSIX for these definitions.
