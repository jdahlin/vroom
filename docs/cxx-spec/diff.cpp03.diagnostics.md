# C.4.8 [[diagnostics]](lib/diagnostics.md): diagnostics library [diff.cpp03.diagnostics]

[Contents](README.md) · up: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · prev: [C.4.7 [[support]](lib/support.md): language support library \[diff.cpp03.language.support\]](diff.cpp03.language.support.md) · next: [C.4.9 [[utilities]](lib/utilities.md): general utilities library \[diff.cpp03.utilities\]](diff.cpp03.utilities.md)

**1** **Affected subclause:** [[errno]](lib/errno.md) <br>**Change:** Thread-local error numbers. <br>**Rationale:** Support for new thread facilities. <br>**Effect on original feature:** Valid but implementation-specific C++ 2003 code that relies on `errno` being the same across threads may change behavior in this International Standard.
