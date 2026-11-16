# 16.5.5.5 Member functions [member.functions]

[Contents](../../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.4 Non-member functions \[global.functions\]](global.functions.md) · next: [16.5.5.6 Friend functions \[hidden.friends\]](hidden.friends.md)

**1** It is unspecified whether any member functions in the C++ standard library are defined as inline ([[dcl.inline]](../../dcl/dcl.inline.md)).

**2** For a non-virtual member function described in the C++ standard library, an implementation may declare a different set of member function signatures, provided that any call to the member function that would select an overload from the set of declarations described in this document behaves as if that overload were selected. [*Note 1*: For instance, an implementation may add parameters with default values, or replace a member function with default arguments with two or more member functions with equivalent behavior, or add additional signatures for a member function name. — *end note*]
