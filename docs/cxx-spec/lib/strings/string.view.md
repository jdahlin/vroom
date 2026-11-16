# 21.4 String view classes [string.view]

[Contents](../../README.md) · up: [21 Strings library \[strings\]](strings.md) · prev: [21.3.6 Suffix for `basic_string` literals \[basic.string.literals\]](basic.string.literals.md) · next: [21.4.1 Header `<string_view>` synopsis \[string.view.synop\]](string.view.synop.md)

**1** The class template `basic_string_view` describes an object that can refer to a constant contiguous sequence of char-like ([[strings.general]](strings.general.md)) objects with the first element of the sequence at position zero. In the rest of this subclause, the type of the char-like objects held in a `basic_string_view` object is designated by `charT`.

**2** [*Note 1*: The library provides implicit conversions from `const charT*` and `std::basic_string<charT, ...>` to `std::basic_string_view<charT, ...>` so that user code can accept just `std::basic_string_view<charT>` as a non-templated parameter wherever a sequence of characters is expected. User-defined types should define their own implicit conversions to `std::basic_string_view` in order to interoperate with these functions. — *end note*]

## Subclauses

- [21.4.1 Header `<string_view>` synopsis \[string.view.synop\]](string.view.synop.md)
- [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md)
  - [21.4.2.1 Construction and assignment \[string.view.cons\]](string.view.cons.md)
  - [21.4.2.2 Iterator support \[string.view.iterators\]](string.view.iterators.md)
  - [21.4.2.3 Capacity \[string.view.capacity\]](string.view.capacity.md)
  - [21.4.2.4 Element access \[string.view.access\]](string.view.access.md)
  - [21.4.2.5 Modifiers \[string.view.modifiers\]](string.view.modifiers.md)
  - [21.4.2.6 String operations \[string.view.ops\]](string.view.ops.md)
  - [21.4.2.7 Searching \[string.view.find\]](string.view.find.md)
- [21.4.3 Deduction guide \[string.view.deduct\]](string.view.deduct.md)
- [21.4.4 Non-member comparison functions \[string.view.comparison\]](string.view.comparison.md)
- [21.4.5 Inserters and extractors \[string.view.io\]](string.view.io.md)
- [21.4.6 Hash support \[string.view.hash\]](string.view.hash.md)
- [21.4.7 Suffix for `basic_string_view` literals \[string.view.literals\]](string.view.literals.md)
