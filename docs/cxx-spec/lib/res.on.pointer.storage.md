# 16.5.5.14 Restrictions on storage of pointers [res.on.pointer.storage]

[Contents](../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.13 Restrictions on exception handling \[res.on.exception.handling\]](res.on.exception.handling.md) · next: [16.5.5.15 Value of error codes \[value.error.codes\]](value.error.codes.md)

**1** Objects constructed by the standard library that may hold a user-supplied pointer value or an integer of type `std::intptr_t` shall store such values in a traceable pointer location ([[basic.stc.dynamic.safety]](../basic.stc.dynamic.safety.md)). [*Note 1*: Other libraries are strongly encouraged to do the same, since not doing so may result in accidental use of pointers that are not safely derived. Libraries that store pointers outside the user’s address space should make it appear that they are stored and retrieved from a traceable pointer location. — *end note*]
