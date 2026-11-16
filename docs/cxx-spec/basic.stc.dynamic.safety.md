# 6.7.5.4.3 Safely-derived pointers [basic.stc.dynamic.safety]

[Contents](README.md) · up: [6.7.5.4 Dynamic storage duration \[basic.stc.dynamic\]](basic.stc.dynamic.md) · prev: [6.7.5.4.2 Deallocation functions \[basic.stc.dynamic.deallocation\]](basic.stc.dynamic.deallocation.md) · next: [6.7.5.5 Duration of subobjects \[basic.stc.inherit\]](basic.stc.inherit.md)

**1** A *traceable pointer object* is

- an object of an object pointer type ([[basic.compound]](basic.compound.md)), or
- an object of an integral type that is at least as large as `std::intptr_t`, or
- a sequence of elements in an array of narrow character type ([[basic.fundamental]](basic.fundamental.md)), where the size and alignment of the sequence match those of some object pointer type.

**2** A pointer value is a *safely-derived pointer* to an object with dynamic storage duration only if the pointer value has an object pointer type and is one of the following:

- the value returned by a call to the C++ standard library implementation of `::operator new(std::size_t)` or `::operator new(std::size_t, std::align_val_t)`;[^1]
- the result of taking the address of an object (or one of its subobjects) designated by an lvalue resulting from indirection through a safely-derived pointer value;
- the result of well-defined pointer arithmetic ([[expr.add]](expr.add.md)) using a safely-derived pointer value;
- the result of a well-defined pointer conversion ([[conv.ptr]](conv.ptr.md), [[expr.type.conv]](expr.type.conv.md), [[expr.static.cast]](expr.static.cast.md), [[expr.cast]](expr.cast.md)) of a safely-derived pointer value;
- the result of a `reinterpret_cast` of a safely-derived pointer value;
- the result of a `reinterpret_cast` of an integer representation of a safely-derived pointer value;
- the value of an object whose value was copied from a traceable pointer object, where at the time of the copy the source object contained a copy of a safely-derived pointer value.

**3** An integer value is an *integer representation of a safely-derived pointer* only if its type is at least as large as `std::intptr_t` and it is one of the following:

- the result of a `reinterpret_cast` of a safely-derived pointer value;
- the result of a valid conversion of an integer representation of a safely-derived pointer value;
- the value of an object whose value was copied from a traceable pointer object, where at the time of the copy the source object contained an integer representation of a safely-derived pointer value;
- the result of an additive or bitwise operation, one of whose operands is an integer representation of a safely-derived pointer value `P`, if that result converted by `reinterpret_cast<void*>` would compare equal to a safely-derived pointer computable from `reinterpret_cast<void*>(P)`.

**4** An implementation may have *relaxed pointer safety*, in which case the validity of a pointer value does not depend on whether it is a safely-derived pointer value. Alternatively, an implementation may have *strict pointer safety*, in which case a pointer value referring to an object with dynamic storage duration that is not a safely-derived pointer value is an invalid pointer value unless the referenced complete object has previously been declared reachable ([[util.dynamic.safety]](lib/util.dynamic.safety.md)). [*Note 1*: The effect of using an invalid pointer value (including passing it to a deallocation function) is undefined, see [[basic.stc]](basic.stc.md). This is true even if the unsafely-derived pointer value might compare equal to some safely-derived pointer value. — *end note*] It is implementation-defined whether an implementation has relaxed or strict pointer safety.

[^1]: This subclause does not impose restrictions on indirection through pointers to memory not allocated by `::operator new`. This maintains the ability of many C++ implementations to use binary libraries and components written in other languages. In particular, this applies to C binaries, because indirection through pointers to memory allocated by `std::malloc` is not restricted.
