# 15.5 Header unit importation [cpp.import]

[Contents](../README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15.4 Module directive \[cpp.module\]](cpp.module.md) · next: [15.6 Macro replacement \[cpp.replace\]](cpp.replace.md)

```bnf
pp-import:
    'export'ₒₚₜ 'import' header-name pp-tokensₒₚₜ ';' new-line
    'export'ₒₚₜ 'import' header-name-tokens pp-tokensₒₚₜ ';' new-line
    'export'ₒₚₜ 'import' pp-tokens ';' new-line
```

**1** A *pp-import* shall not appear in a context where `import` or (if it is the first token of the *pp-import*) `export` is an identifier defined as an object-like macro.

**2** The preprocessing tokens after the `import` preprocessing token in the `import` *control-line* are processed just as in normal text (i.e., each identifier currently defined as a macro name is replaced by its replacement list of preprocessing tokens). An `import` directive matching the first two forms of a *pp-import* instructs the preprocessor to import macros from the header unit ([[module.import]](../module/module.import.md)) denoted by the *header-name*. The *point of macro import* for the first two forms of *pp-import* is immediately after the *new-line* terminating the *pp-import*. The last form of *pp-import* is only considered if the first two forms did not match.

**3** If a *pp-import* is produced by source file inclusion (including by the rewrite produced when a `#include` directive names an importable header) while processing the *group* of a *module-file*, the program is ill-formed.

**4** In all three forms of *pp-import*, the `import` and `export` (if it exists) preprocessing tokens are replaced by the *import-keyword* and *export-keyword* preprocessing tokens respectively. [*Note 1*: This makes the line no longer a directive so it is not removed at the end of phase 4. — *end note*] Additionally, in the second form of *pp-import*, a *header-name* token is formed as if the *header-name-tokens* were the *pp-tokens* of a `#include` directive. The *header-name-tokens* are replaced by the *header-name* token. [*Note 2*: This ensures that imports are treated consistently by the preprocessor and later phases of translation. — *end note*]

**5** Each `#define` directive encountered when preprocessing each translation unit in a program results in a distinct *macro definition*. [*Note 3*: A predefined macro name ([[cpp.predefined]](cpp.predefined.md)) is not introduced by a `#define` directive. Implementations providing mechanisms to predefine additional macros are encouraged to not treat them as being introduced by a `#define` directive. — *end note*] Importing macros from a header unit makes macro definitions from a translation unit visible in other translation units. Each macro definition has at most one point of definition in each translation unit and at most one point of undefinition, as follows:

- The *point of definition* of a macro definition within a translation unit is the point at which its `#define` directive occurs (in the translation unit containing the `#define` directive), or, if the macro name is not lexically identical to a keyword ([[lex.key]](../lex/lex.key.md)) or to the *identifier*s `module` or `import`, the first point of macro import of a translation unit containing a point of definition for the macro definition, if any (in any other translation unit).
- The *point of undefinition* of a macro definition within a translation unit is the first point at which a `#undef` directive naming the macro occurs after its point of definition, or the first point of macro import of a translation unit containing a point of undefinition for the macro definition, whichever (if any) occurs first.

**6** A macro directive is *active* at a source location if it has a point of definition in that translation unit preceding the location, and does not have a point of undefinition in that translation unit preceding the location.

**7** If a macro would be replaced or redefined, and multiple macro definitions are active for that macro name, the active macro definitions shall all be valid redefinitions of the same macro ([[cpp.replace]](cpp.replace.md)). [*Note 4*: The relative order of *pp-import*s has no bearing on whether a particular macro definition is active. — *end note*]

**8** [*Example 1*:

*Importable header `"a.h"`:*

```cpp
#define X 123   // #1
#define Y 45    // #2
#define Z a     // #3
#undef X        // point of undefinition of #1 in "a.h"
```

*Importable header `"b.h"`:*

```cpp
import "a.h";   // point of definition of #1, #2, and #3, point of undefinition of #1 in "b.h"
#define X 456   // OK, #1 is not active
#define Y 6     // error: #2 is active
```

*Importable header `"c.h"`:*

```cpp
#define Y 45    // #4
#define Z c     // #5
```

*Importable header `"d.h"`:*

```cpp
import "a.h";   // point of definition of #1, #2, and #3, point of undefinition of #1 in "d.h"
import "c.h";   // point of definition of #4 and #5 in "d.h"
int a = Y;      // OK, active macro definitions #2 and #4 are valid redefinitions
int c = Z;      // error: active macro definitions #3 and #5 are not valid redefinitions of Z
```

— *end example*]
