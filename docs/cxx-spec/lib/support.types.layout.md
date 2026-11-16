# 17.2.4 Sizes, alignments, and offsets [support.types.layout]

[Contents](../README.md) · up: [17.2 Common definitions \[support.types\]](support.types.md) · prev: [17.2.3 Null pointers \[support.types.nullptr\]](support.types.nullptr.md) · next: [17.2.5 `byte` type operations \[support.types.byteops\]](support.types.byteops.md)

**1** The macro `offsetof(type, member-designator)` has the same semantics as the corresponding macro in the C standard library header `<stddef.h>`, but accepts a restricted set of `type` arguments in this document. Use of the `offsetof` macro with a `type` other than a standard-layout class ([[class.prop]](../class.prop.md)) is conditionally-supported.[^1] The expression `offsetof(type, member-designator)` is never type-dependent ([[temp.dep.expr]](../temp.dep.expr.md)) and it is value-dependent ([[temp.dep.constexpr]](../temp.dep.constexpr.md)) if and only if `type` is dependent. The result of applying the `offsetof` macro to a static data member or a function member is undefined. No operation invoked by the `offsetof` macro shall throw an exception and `noexcept(offsetof(type, member-designator))` shall be `true`.

**2** The type `ptrdiff_t` is an implementation-defined signed integer type that can hold the difference of two subscripts in an array object, as described in [[expr.add]](../expr.add.md).

**3** The type `size_t` is an implementation-defined unsigned integer type that is large enough to contain the size in bytes of any object ([[expr.sizeof]](../expr.sizeof.md)).

**4** [*Note 1*: It is recommended that implementations choose types for `ptrdiff_t` and `size_t` whose integer conversion ranks ([[conv.rank]](../conv.rank.md)) are no greater than that of `signed long int` unless a larger size is necessary to contain all the possible values. — *end note*]

**5** The type `max_align_t` is a trivial standard-layout type whose alignment requirement is at least as great as that of every scalar type, and whose alignment requirement is supported in every context ([[basic.align]](../basic.align.md)).

*See also:* ISO C 7.19

[^1]: Note that `offsetof` is required to work as specified even if unary `operator&` is overloaded for any of the types involved.
