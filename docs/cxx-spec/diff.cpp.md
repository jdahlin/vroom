# C.5.7 [[cpp]](cpp.md): preprocessing directives [diff.cpp]

[Contents](README.md) · up: [C.5 C++ and ISO C \[diff.iso\]](diff.iso.md) · prev: [C.5.6 [[class]](class.md): classes \[diff.class\]](diff.class.md) · next: [C.6 C standard library \[diff.library\]](diff.library.md)

**1** **Affected subclause:** [[cpp.predefined]](cpp.predefined.md) <br>**Change:** Whether `__STDC__` is defined and if so, what its value is, are implementation-defined. <br>**Rationale:** C++ is not identical to ISO C. Mandating that `__STDC__` be defined would require that translators make an incorrect claim. Each implementation must choose the behavior that will be most useful to its marketplace. <br>**Effect on original feature:** Change to semantics of well-defined feature. <br>**Difficulty of converting:** Semantic transformation. <br>**How widely used:** Programs and headers that reference `__STDC__` are quite common.
