# 15.3 Source file inclusion [cpp.include]

[Contents](README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15.2 Conditional inclusion \[cpp.cond\]](cpp.cond.md) · next: [15.4 Module directive \[cpp.module\]](cpp.module.md)

**1** A `#include` directive shall identify a header or source file that can be processed by the implementation.

**2** A preprocessing directive of the form

```bnf
'# include <' h-char-sequence '>' new-line
```

searches a sequence of implementation-defined places for a header identified uniquely by the specified sequence between the `<` and `>` delimiters, and causes the replacement of that directive by the entire contents of the header. How the places are specified or the header identified is implementation-defined.

**3** A preprocessing directive of the form

```bnf
'# include "' q-char-sequence '"' new-line
```

causes the replacement of that directive by the entire contents of the source file identified by the specified sequence between the `"` delimiters. The named source file is searched for in an implementation-defined manner. If this search is not supported, or if the search fails, the directive is reprocessed as if it read

```bnf
'# include <' h-char-sequence '>' new-line
```

with the identical contained sequence (including `>` characters, if any) from the original directive.

**4** A preprocessing directive of the form

```bnf
'# include' pp-tokens new-line
```

(that does not match one of the two previous forms) is permitted. The preprocessing tokens after `include` in the directive are processed just as in normal text (i.e., each identifier currently defined as a macro name is replaced by its replacement list of preprocessing tokens). If the directive resulting after all replacements does not match one of the two previous forms, the behavior is undefined.[^1] The method by which a sequence of preprocessing tokens between a `<` and a `>` preprocessing token pair or a pair of `"` characters is combined into a single header name preprocessing token is implementation-defined.

**5** The implementation shall provide unique mappings for sequences consisting of one or more *nondigit*s or *digit*s ([[lex.name]](lex.name.md)) followed by a period (`.`) and a single *nondigit*. The first character shall not be a *digit*. The implementation may ignore distinctions of alphabetical case.

**6** A `#include` preprocessing directive may appear in a source file that has been read because of a `#include` directive in another file, up to an implementation-defined nesting limit.

**7** If the header identified by the *header-name* denotes an importable header ([[module.import]](module.import.md)), it is implementation-defined whether the `#include` preprocessing directive is instead replaced by an `import` directive ([[cpp.import]](cpp.import.md)) of the form

```bnf
'import' header-name ';' new-line
```

**8** [*Note 1*: Although an implementation may provide a mechanism for making arbitrary source files available to the `< >` search, in general programmers should use the `< >` form for headers provided with the implementation, and the `" "` form for sources outside the control of the implementation. For instance:

```cpp
#include <stdio.h>
#include <unistd.h>
#include "usefullib.h"
#include "myprog.h"
```

— *end note*]

**9** [*Example 1*: This illustrates macro-replaced `#include` directives:

```cpp
#if VERSION == 1
    #define INCFILE  "vers1.h"
#elif VERSION == 2
    #define INCFILE  "vers2.h"  // and so on
#else
    #define INCFILE  "versN.h"
#endif
#include INCFILE
```

— *end example*]

[^1]: Note that adjacent *string-literal*s are not concatenated into a single *string-literal* (see the translation phases in [[lex.phases]](lex.phases.md)); thus, an expansion that results in two *string-literal*s is an invalid directive.
