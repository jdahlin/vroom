# 16.5.2.2 Headers [using.headers]

[Contents](../README.md) · up: [16.5.2 Using the library \[using\]](using.md) · prev: [16.5.2.1 Overview \[using.overview\]](using.overview.md) · next: [16.5.2.3 Linkage \[using.linkage\]](using.linkage.md)

**1** The entities in the C++ standard library are defined in headers, whose contents are made available to a translation unit when it contains the appropriate `#include` preprocessing directive ([[cpp.include]](../cpp.include.md)) or the appropriate `import` declaration ([[module.import]](../module.import.md)).

**2** A translation unit may include library headers in any order ([[lex.separate]](../lex.separate.md)). Each may be included more than once, with no effect different from being included exactly once, except that the effect of including either `<cassert>` ([[cassert.syn]](cassert.syn.md)) or `<assert.h>` ([[depr.c.headers]](../depr.c.headers.md)) depends each time on the lexically current definition of `NDEBUG`.[^1]

**3** A translation unit shall include a header only outside of any declaration or definition and, in the case of a module unit, only in its *global-module-fragment*, and shall include the header or import the corresponding header unit lexically before the first reference in that translation unit to any of the entities declared in that header. No diagnostic is required.

[^1]: This is the same as the C standard library.
