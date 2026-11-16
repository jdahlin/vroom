# C.6.1 Modifications to headers [diff.mods.to.headers]

[Contents](README.md) · up: [C.6 C standard library \[diff.library\]](diff.library.md) · prev: [C.6 C standard library \[diff.library\]](diff.library.md) · next: [C.6.2 Modifications to definitions \[diff.mods.to.definitions\]](diff.mods.to.definitions.md)

**1** For compatibility with the C standard library, the C++ standard library provides the C headers enumerated in [[depr.c.headers]](depr.c.headers.md), but their use is deprecated in C++.

**2** There are no C++ headers for the C standard library’s headers `<stdatomic.h>`, `<stdnoreturn.h>`, and `<threads.h>`, nor are these headers from the C standard library headers themselves part of C++.

**3** The C headers `<complex.h>` and `<tgmath.h>` do not contain any of the content from the C standard library and instead merely include other headers from the C++ standard library.
