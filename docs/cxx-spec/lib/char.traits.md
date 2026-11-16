# 21.2 Character traits [char.traits]

[Contents](../README.md) · up: [21 Strings library \[strings\]](strings.md) · prev: [21.1 General \[strings.general\]](strings.general.md) · next: [21.2.1 Character traits requirements \[char.traits.require\]](char.traits.require.md)

**1** This subclause defines requirements on classes representing *character traits*, and defines a class template `char_traits<charT>`, along with five specializations, `char_traits<char>`, `char_traits<char8_t>`, `char_traits<char16_t>`, `char_traits<char32_t>`, and `char_traits<wchar_t>`, that meet those requirements.

**2** Most classes specified in [[string.classes]](string.classes.md), [[string.view]](string.view.md), and [[input.output]](input.output.md) need a set of related types and functions to complete the definition of their semantics. These types and functions are provided as a set of member *typedef-name*s and functions in the template parameter `traits` used by each such template. This subclause defines the semantics of these members.

**3** To specialize those templates to generate a string, string view, or iostream class to handle a particular character container type ([[defns.character.container]](definitions.md#defns.character.container)) `C`, that and its related character traits class `X` are passed as a pair of parameters to the string, string view, or iostream template as parameters `charT` and `traits`. If `X::char_type` is not the same type as `C`, the program is ill-formed.

## Subclauses

- [21.2.1 Character traits requirements \[char.traits.require\]](char.traits.require.md)
- [21.2.2 Traits typedefs \[char.traits.typedefs\]](char.traits.typedefs.md)
- [21.2.3 `char_traits` specializations \[char.traits.specializations\]](char.traits.specializations.md)
  - [21.2.3.1 `struct char_traits<char>` \[char.traits.specializations.char\]](char.traits.specializations.char.md)
  - [21.2.3.2 `struct char_traits<char8_t>` \[char.traits.specializations.char8.t\]](char.traits.specializations.char8.t.md)
  - [21.2.3.3 `struct char_traits<char16_t>` \[char.traits.specializations.char16.t\]](char.traits.specializations.char16.t.md)
  - [21.2.3.4 `struct char_traits<char32_t>` \[char.traits.specializations.char32.t\]](char.traits.specializations.char32.t.md)
  - [21.2.3.5 `struct char_traits<wchar_t>` \[char.traits.specializations.wchar.t\]](char.traits.specializations.wchar.t.md)
