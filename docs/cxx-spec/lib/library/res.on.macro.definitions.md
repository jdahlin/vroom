# 16.5.5.3 Restrictions on macro definitions [res.on.macro.definitions]

[Contents](../../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.2 Headers \[res.on.headers\]](res.on.headers.md) · next: [16.5.5.4 Non-member functions \[global.functions\]](global.functions.md)

**1** The names and global function signatures described in [[contents]](contents.md) are reserved to the implementation.

**2** All object-like macros defined by the C standard library and described in this Clause as expanding to integral constant expressions are also suitable for use in `#if` preprocessing directives, unless explicitly stated otherwise.
