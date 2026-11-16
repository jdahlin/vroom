# 29.11.4 Requirements [fs.req]

[Contents](../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.3 Normative references \[fs.norm.ref\]](fs.norm.ref.md) · next: [29.11.4.1 Namespaces and headers \[fs.req.namespace\]](fs.req.namespace.md)

**1** Throughout subclause [[filesystems]](filesystems.md), `char`, `wchar_t`, `char8_t`, `char16_t`, and `char32_t` are collectively called *encoded character types*.

**2** Functions with template parameters named `EcharT` shall not participate in overload resolution unless `EcharT` is one of the encoded character types.

**3** Template parameters named `InputIterator` shall meet the *Cpp17InputIterator* requirements ([[input.iterators]](input.iterators.md)) and shall have a value type that is one of the encoded character types.

**4** [*Note 1*: Use of an encoded character type implies an associated character set and encoding. Since `signed char` and `unsigned char` have no implied character set and encoding, they are not included as permitted types. — *end note*]

**5** Template parameters named `Allocator` shall meet the *Cpp17Allocator* requirements ([Table 36](allocator.requirements.md#tab:cpp17.allocator)).

## Subclauses

- [29.11.4.1 Namespaces and headers \[fs.req.namespace\]](fs.req.namespace.md)
