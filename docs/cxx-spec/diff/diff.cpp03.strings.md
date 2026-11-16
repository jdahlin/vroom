# C.4.10 [[strings]](../lib/strings/strings.md): strings library [diff.cpp03.strings]

[Contents](../README.md) · up: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · prev: [C.4.9 [[utilities]](../lib/utilities/utilities.md): general utilities library \[diff.cpp03.utilities\]](diff.cpp03.utilities.md) · next: [C.4.11 [[containers]](../lib/containers/containers.md): containers library \[diff.cpp03.containers\]](diff.cpp03.containers.md)

**1** **Affected subclause:** [[string.classes]](../lib/strings/string.classes.md) <br>**Change:** `basic_string` requirements no longer allow reference-counted strings. <br>**Rationale:** Invalidation is subtly different with reference-counted strings. This change regularizes behavior for this International Standard. <br>**Effect on original feature:** Valid C++ 2003 code may execute differently in this International Standard.

**2** **Affected subclause:** [[string.require]](../lib/strings/string.require.md) <br>**Change:** Loosen `basic_string` invalidation rules. <br>**Rationale:** Allow small-string optimization. <br>**Effect on original feature:** Valid C++ 2003 code may execute differently in this International Standard. Some `const` member functions, such as `data` and `c_str`, no longer invalidate iterators.
