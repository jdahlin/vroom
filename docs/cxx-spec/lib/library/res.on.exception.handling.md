# 16.5.5.13 Restrictions on exception handling [res.on.exception.handling]

[Contents](../../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.12 Derived classes \[derivation\]](derivation.md) · next: [16.5.5.14 Restrictions on storage of pointers \[res.on.pointer.storage\]](res.on.pointer.storage.md)

**1** Any of the functions defined in the C++ standard library can report a failure by throwing an exception of a type described in its *Throws:* paragraph, or of a type derived from a type named in the *Throws:* paragraph that would be caught by an exception handler for the base type.

**2** Functions from the C standard library shall not throw exceptions[^1] except when such a function calls a program-supplied function that throws an exception.[^2]

**3** Destructor operations defined in the C++ standard library shall not throw exceptions. Every destructor in the C++ standard library shall behave as if it had a non-throwing exception specification.

**4** Functions defined in the C++ standard library that do not have a *Throws:* paragraph but do have a potentially-throwing exception specification may throw implementation-defined exceptions.[^3] Implementations should report errors by throwing exceptions of or derived from the standard exception classes ([[bad.alloc]](../support/bad.alloc.md), [[support.exception]](../support/support.exception.md), [[std.exceptions]](../diagnostics/std.exceptions.md)).

**5** An implementation may strengthen the exception specification for a non-virtual function by adding a non-throwing exception specification.

[^1]: That is, the C library functions can all be treated as if they are marked `noexcept`. This allows implementations to make performance optimizations based on the absence of exceptions at runtime.
[^2]: The functions `qsort()` and `bsearch()` ([[alg.c.library]](../algorithms/alg.c.library.md)) meet this condition.
[^3]: In particular, they can report a failure to allocate storage by throwing an exception of type `bad_alloc`, or a class derived from `bad_alloc` ([[bad.alloc]](../support/bad.alloc.md)).
