# 6.8.2 Compound types [basic.compound]

[Contents](../README.md) · up: [6.8 Types \[basic.types\]](basic.types.md) · prev: [6.8.1 Fundamental types \[basic.fundamental\]](basic.fundamental.md) · next: [6.8.3 CV-qualifiers \[basic.type.qualifier\]](basic.type.qualifier.md)

**1** Compound types can be constructed in the following ways:

- *arrays* of objects of a given type, [[dcl.array]](../dcl/dcl.array.md);

- *functions*, which have parameters of given types and return `void` or references or objects of a given type, [[dcl.fct]](../dcl/dcl.fct.md);

- *pointers* to *cv* `void` or objects or functions (including static members of classes) of a given type, [[dcl.ptr]](../dcl/dcl.ptr.md);

- *references* to objects or functions of a given type, [[dcl.ref]](../dcl/dcl.ref.md). There are two types of references:

  - lvalue reference
  - rvalue reference

- *classes* containing a sequence of objects of various types ([[class]](../class/class.md)), a set of types, enumerations and functions for manipulating these objects ([[class.mfct]](../class/class.mfct.md)), and a set of restrictions on the access to these entities ([[class.access]](../class/class.access.md));

- *unions*, which are classes capable of containing objects of different types at different times, [[class.union]](../class/class.union.md);

- *enumerations*, which comprise a set of named constant values. Each distinct enumeration constitutes a different *enumerated type*, [[dcl.enum]](../dcl/dcl.enum.md);

- *pointers to non-static class members*,[^1] which identify members of a given type within objects of a given class, [[dcl.mptr]](../dcl/dcl.mptr.md). Pointers to data members and pointers to member functions are collectively called *pointer-to-member* types.

**2** These methods of constructing types can be applied recursively; restrictions are mentioned in [[dcl.meaning]](../dcl/dcl.meaning.md). Constructing a type such that the number of bytes in its object representation exceeds the maximum value representable in the type `std::size_t` ([[support.types]](../lib/support/support.types.md)) is ill-formed.

**3** The type of a pointer to *cv* `void` or a pointer to an object type is called an *object pointer type*. [*Note 1*: A pointer to `void` does not have a pointer-to-object type, however, because `void` is not an object type. — *end note*] The type of a pointer that can designate a function is called a *function pointer type*. A pointer to an object of type `T` is referred to as a “pointer to `T`”. [*Example 1*: A pointer to an object of type `int` is referred to as “pointer to `int`” and a pointer to an object of class `X` is called a “pointer to `X`”. — *end example*] Except for pointers to static members, text referring to “pointers” does not apply to pointers to members. Pointers to incomplete types are allowed although there are restrictions on what can be done with them ([[basic.align]](basic.align.md)). Every value of pointer type is one of the following:

- a *pointer to* an object or function (the pointer is said to *point* to the object or function), or
- a *pointer past the end of* an object ([[expr.add]](../expr/expr.add.md)), or
- the *null pointer value* for that type, or
- an *invalid pointer value*.

A value of a pointer type that is a pointer to or past the end of an object *represents the address* of the first byte in memory ([[intro.memory]](intro.memory.md)) occupied by the object[^2] or the first byte in memory after the end of the storage occupied by the object, respectively. [*Note 2*: A pointer past the end of an object ([[expr.add]](../expr/expr.add.md)) is not considered to point to an unrelated object of the object’s type that might be located at that address. A pointer value becomes invalid when the storage it denotes reaches the end of its storage duration; see [[basic.stc]](basic.stc.md). — *end note*] For purposes of pointer arithmetic ([[expr.add]](../expr/expr.add.md)) and comparison ([[expr.rel]](../expr/expr.rel.md), [[expr.eq]](../expr/expr.eq.md)), a pointer past the end of the last element of an array `x` of *n* elements is considered to be equivalent to a pointer to a hypothetical array element *n* of `x` and an object of type `T` that is not an array element is considered to belong to an array with one element of type `T`. The value representation of pointer types is implementation-defined. Pointers to layout-compatible types shall have the same value representation and alignment requirements ([[basic.align]](basic.align.md)). [*Note 3*: Pointers to over-aligned types ([[basic.align]](basic.align.md)) have no special representation, but their range of valid values is restricted by the extended alignment requirement. — *end note*]

**4** Two objects *a* and *b* are *pointer-interconvertible* if:

- they are the same object, or
- one is a union object and the other is a non-static data member of that object ([[class.union]](../class/class.union.md)), or
- one is a standard-layout class object and the other is the first non-static data member of that object, or, if the object has no non-static data members, any base class subobject of that object ([[class.mem]](../class/class.mem.md)), or
- there exists an object *c* such that *a* and *c* are pointer-interconvertible, and *c* and *b* are pointer-interconvertible.

If two objects are pointer-interconvertible, then they have the same address, and it is possible to obtain a pointer to one from a pointer to the other via a `reinterpret_cast` ([[expr.reinterpret.cast]](../expr/expr.reinterpret.cast.md)). [*Note 4*: An array object and its first element are not pointer-interconvertible, even though they have the same address. — *end note*]

**5** A pointer to *cv* `void` can be used to point to objects of unknown type. Such a pointer shall be able to hold any object pointer. An object of type *cv* `void*` shall have the same representation and alignment requirements as *cv* `char*`.

[^1]: Static class members are objects or functions, and pointers to them are ordinary pointers to objects or functions.
[^2]: For an object that is not within its lifetime, this is the first byte in memory that it will occupy or used to occupy.
