# 16.4.2.2.5 Character sequences [character.seq]

[Contents](../README.md) · up: [16.4.2.2 Type descriptions \[type.descriptions\]](type.descriptions.md) · prev: [16.4.2.2.4 Bitmask types \[bitmask.types\]](bitmask.types.md) · next: [16.4.2.2.5.1 Byte strings \[byte.strings\]](byte.strings.md)

**1** The C standard library makes widespread use of characters and character sequences that follow a few uniform conventions:

- A *letter* is any of the 26 lowercase or 26 uppercase letters in the basic execution character set.
- The *decimal-point character* is the (single-byte) character used by functions that convert between a (single-byte) character sequence and a value of one of the floating-point types. It is used in the character sequence to denote the beginning of a fractional part. It is represented in [[support]](support.md) through [[thread]](thread.md) and [[depr]](../depr.md) by a period, `'.'`, which is also its value in the `"C"` locale, but may change during program execution by a call to `setlocale(int, const char*)`,[^1] or by a change to a `locale` object, as described in [[locales]](locales.md) and [[input.output]](input.output.md).
- A *character sequence* is an array object ([[dcl.array]](../dcl.array.md)) `A` that can be declared as `T A[N]`, where `T` is any of the types `char`, `unsigned char`, or `signed char` ([[basic.fundamental]](../basic.fundamental.md)), optionally qualified by any combination of `const` or `volatile`. The initial elements of the array have defined contents up to and including an element determined by some predicate. A character sequence can be designated by a pointer value `S` that points to its first element.

## Subclauses

- [16.4.2.2.5.1 Byte strings \[byte.strings\]](byte.strings.md)
- [16.4.2.2.5.2 Multibyte strings \[multibyte.strings\]](multibyte.strings.md)

[^1]: declared in `<clocale>` ([[clocale.syn]](clocale.syn.md)).
