# 6.7.6 Alignment [basic.align]

[Contents](../README.md) · up: [6.7 Memory and objects \[basic.memobj\]](basic.memobj.md) · prev: [6.7.5.5 Duration of subobjects \[basic.stc.inherit\]](basic.stc.inherit.md) · next: [6.7.7 Temporary objects \[class.temporary\]](class.temporary.md)

**1** Object types have *alignment requirements* ([[basic.fundamental]](basic.fundamental.md), [[basic.compound]](basic.compound.md)) which place restrictions on the addresses at which an object of that type may be allocated. An *alignment* is an implementation-defined integer value representing the number of bytes between successive addresses at which a given object can be allocated. An object type imposes an alignment requirement on every object of that type; stricter alignment can be requested using the alignment specifier ([[dcl.align]](../dcl/dcl.align.md)).

**2** A *fundamental alignment* is represented by an alignment less than or equal to the greatest alignment supported by the implementation in all contexts, which is equal to `alignof(std::max_align_t)` ([[support.types]](../lib/support/support.types.md)). The alignment required for a type might be different when it is used as the type of a complete object and when it is used as the type of a subobject. [*Example 1*:

```cpp
struct B { long double d; };
struct D : virtual B { char c; };
```

When `D` is the type of a complete object, it will have a subobject of type `B`, so it must be aligned appropriately for a `long double`. If `D` appears as a subobject of another object that also has `B` as a virtual base class, the `B` subobject might be part of a different subobject, reducing the alignment requirements on the `D` subobject. — *end example*] The result of the `alignof` operator reflects the alignment requirement of the type in the complete-object case.

**3** An *extended alignment* is represented by an alignment greater than `alignof(std::max_align_t)`. It is implementation-defined whether any extended alignments are supported and the contexts in which they are supported ([[dcl.align]](../dcl/dcl.align.md)). A type having an extended alignment requirement is an *over-aligned type*. [*Note 1*: Every over-aligned type is or contains a class type to which extended alignment applies (possibly through a non-static data member). — *end note*] A *new-extended alignment* is represented by an alignment greater than `__STDCPP_DEFAULT_NEW_ALIGNMENT__` ([[cpp.predefined]](../cpp/cpp.predefined.md)).

**4** Alignments are represented as values of the type `std::size_t`. Valid alignments include only those values returned by an `alignof` expression for the fundamental types plus an additional implementation-defined set of values, which may be empty. Every alignment value shall be a non-negative integral power of two.

**5** Alignments have an order from *weaker* to *stronger* or *stricter* alignments. Stricter alignments have larger alignment values. An address that satisfies an alignment requirement also satisfies any weaker valid alignment requirement.

**6** The alignment requirement of a complete type can be queried using an `alignof` expression ([[expr.alignof]](../expr/expr.alignof.md)). Furthermore, the narrow character types ([[basic.fundamental]](basic.fundamental.md)) shall have the weakest alignment requirement. [*Note 2*: This enables the ordinary character types to be used as the underlying type for an aligned memory area ([[dcl.align]](../dcl/dcl.align.md)). — *end note*]

**7** Comparing alignments is meaningful and provides the obvious results:

- Two alignments are equal when their numeric values are equal.
- Two alignments are different when their numeric values are not equal.
- When an alignment is larger than another it represents a stricter alignment.

**8** [*Note 3*: The runtime pointer alignment function ([[ptr.align]](../lib/utilities/ptr.align.md)) can be used to obtain an aligned pointer within a buffer; the aligned-storage templates in the library ([[meta.trans.other]](../lib/utilities/meta.trans.other.md)) can be used to obtain aligned storage. — *end note*]

**9** If a request for a specific extended alignment in a specific context is not supported by an implementation, the program is ill-formed.
