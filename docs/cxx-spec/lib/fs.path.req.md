# 29.11.7.3 Requirements [fs.path.req]

[Contents](../README.md) · up: [29.11.7 Class `path` \[fs.class.path\]](fs.class.path.md) · prev: [29.11.7.2.2 Type and encoding conversions \[fs.path.type.cvt\]](fs.path.type.cvt.md) · next: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md)

**1** In addition to the requirements ([[fs.req]](fs.req.md)), function template parameters named `Source` shall be one of:

- `basic_string<EcharT, traits, Allocator>`. A function argument `const Source&` `source` shall have an effective range `[source.begin(), source.end())`.
- `basic_string_view<EcharT, traits>`. A function argument `const Source&` `source` shall have an effective range `[source.begin(), source.end())`.
- A type meeting the *Cpp17InputIterator* requirements that iterates over a NTCTS. The value type shall be an encoded character type. A function argument `const Source&` `source` shall have an effective range `[source, end)` where `end` is the first iterator value with an element value equal to `iterator_traits<Source>::value_type()`.
- A character array that after array-to-pointer decay results in a pointer to the start of a NTCTS. The value type shall be an encoded character type. A function argument `const Source&` `source` shall have an effective range `[source, end)` where `end` is the first iterator value with an element value equal to `iterator_traits<decay_t<Source>>::value_type()`.

**2** Functions taking template parameters named `Source` shall not participate in overload resolution unless `Source` denotes a type other than `path`, and either

- `Source` is a specialization of `basic_string` or `basic_string_view`, or
- the *qualified-id* `iterator_traits<decay_t<Source>>::value_type` is valid and denotes a possibly `const` encoded character type ([[temp.deduct]](../temp.deduct.md)).

**3** [*Note 1*: See path conversions ([[fs.path.cvt]](fs.path.cvt.md)) for how the value types above and their encodings convert to `path::value_type` and its encoding. — *end note*]

**4** Arguments of type `Source` shall not be null pointers.
