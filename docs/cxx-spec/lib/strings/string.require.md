# 21.3.2.1 General requirements [string.require]

[Contents](../../README.md) · up: [21.3.2 Class template `basic_string` \[basic.string\]](basic.string.md) · prev: [21.3.2 Class template `basic_string` \[basic.string\]](basic.string.md) · next: [21.3.2.2 Constructors and assignment operators \[string.cons\]](string.cons.md)

**1** If any operation would cause `size()` to exceed `max_size()`, that operation throws an exception object of type `length_error`.

**2** If any member function or operator of `basic_string` throws an exception, that function or operator has no other effect on the `basic_string` object.

**3** In every specialization `basic_string<charT, traits, Allocator>`, the type `allocator_traits<Allocator>::value_type` shall name the same type as `charT`. Every object of type `basic_string<charT, traits, Allocator>` uses an object of type `Allocator` to allocate and free storage for the contained `charT` objects as needed. The `Allocator` object used is obtained as described in [[container.requirements.general]](../containers/container.requirements.general.md). In every specialization `basic_string<charT, traits, Allocator>`, the type `traits` shall meet the character traits requirements ([[char.traits]](char.traits.md)). [*Note 1*: The program is ill-formed if `traits::char_type` is not the same type as `charT`. — *end note*]

**4** References, pointers, and iterators referring to the elements of a `basic_string` sequence may be invalidated by the following uses of that `basic_string` object:

- Passing as an argument to any standard library function taking a reference to non-const `basic_string` as an argument.[^1]
- Calling non-const member functions, except `operator[]`, `at`, `data`, `front`, `back`, `begin`, `rbegin`, `end`, and `rend`.

[^1]: For example, as an argument to non-member functions `swap()` ([[string.special]](string.special.md)), `operator>>()` ([[string.io]](string.io.md)), and `getline()` ([[string.io]](string.io.md)), or as an argument to `basic_string::swap()`.
