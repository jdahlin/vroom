# 16.5.5.12 Derived classes [derivation]

[Contents](../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.11 Protection within classes \[protection.within.classes\]](protection.within.classes.md) · next: [16.5.5.13 Restrictions on exception handling \[res.on.exception.handling\]](res.on.exception.handling.md)

**1** An implementation may derive any class in the C++ standard library from a class with a name reserved to the implementation.

**2** Certain classes defined in the C++ standard library are required to be derived from other classes in the C++ standard library. An implementation may derive such a class directly from the required base or indirectly through a hierarchy of base classes with names reserved to the implementation.

**3** In any case:

- Every base class described as `virtual` shall be virtual;
- Every base class not specified as `virtual` shall not be virtual;
- Unless explicitly stated otherwise, types with distinct names shall be distinct types.[^1]

**4** All types specified in the C++ standard library shall be non-`final` types unless otherwise specified.

[^1]: There is an implicit exception to this rule for types that are described as synonyms for basic integral types, such as `size_t` ([[support.types]](support.types.md)) and `streamoff` ([[stream.types]](stream.types.md)).
