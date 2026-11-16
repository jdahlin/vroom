# 15.1 Preamble [cpp.pre]

[Contents](README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15 Preprocessing directives \[cpp\]](cpp.md) · next: [15.2 Conditional inclusion \[cpp.cond\]](cpp.cond.md)

```bnf
preprocessing-file:
    groupₒₚₜ
    module-file
```

```bnf
module-file:
    pp-global-module-fragmentₒₚₜ pp-module groupₒₚₜ pp-private-module-fragmentₒₚₜ
```

```bnf
pp-global-module-fragment:
    'module' ';' new-line groupₒₚₜ
```

```bnf
pp-private-module-fragment:
    'module' ':' 'private' ';' new-line groupₒₚₜ
```

```bnf
group:
    group-part
    group group-part
```

```bnf
group-part:
    control-line
    if-section
    text-line
    '#' conditionally-supported-directive
```

```bnf
control-line:
    '# include' pp-tokens new-line
    pp-import
    '# define' identifier replacement-list new-line
    '# define' identifier lparen identifier-listₒₚₜ ')' replacement-list new-line
    '# define' identifier lparen '... )' replacement-list new-line
    '# define' identifier lparen identifier-list ', ... )' replacement-list new-line
    '# undef' identifier new-line
    '# line' pp-tokens new-line
    '# error' pp-tokensₒₚₜ new-line
    '# pragma' pp-tokensₒₚₜ new-line
    '#'new-line
```

```bnf
if-section:
    if-group elif-groupsₒₚₜ else-groupₒₚₜ endif-line
```

```bnf
if-group:
    '# if' constant-expression new-line groupₒₚₜ
    '# ifdef' identifier new-line groupₒₚₜ
    '# ifndef' identifier new-line groupₒₚₜ
```

```bnf
elif-groups:
    elif-group
    elif-groups elif-group
```

```bnf
elif-group:
    '# elif' constant-expression new-line groupₒₚₜ
```

```bnf
else-group:
    '# else' new-line groupₒₚₜ
```

```bnf
endif-line:
    '# endif' new-line
```

```bnf
text-line:
    pp-tokensₒₚₜ new-line
```

```bnf
conditionally-supported-directive:
    pp-tokens new-line
```

```bnf
lparen:
    a '(' character not immediately preceded by white-space
```

```bnf
identifier-list:
    identifier
    identifier-list ',' identifier
```

```bnf
replacement-list:
    pp-tokensₒₚₜ
```

```bnf
pp-tokens:
    preprocessing-token
    pp-tokens preprocessing-token
```

```bnf
new-line:
    the new-line character
```

**1** A *preprocessing directive* consists of a sequence of preprocessing tokens that satisfies the following constraints: At the start of translation phase 4, the first token in the sequence, referred to as a *directive-introducing token*, begins with the first character in the source file (optionally after white space containing no new-line characters) or follows white space containing at least one new-line character, and is

- a `#` preprocessing token, or
- an `import` preprocessing token immediately followed on the same logical line by a *header-name*, `<`, *identifier*, *string-literal*, or `:` preprocessing token, or
- a `module` preprocessing token immediately followed on the same logical line by an *identifier*, `:`, or `;` preprocessing token, or
- an `export` preprocessing token immediately followed on the same logical line by one of the two preceding forms.

The last token in the sequence is the first token within the sequence that is immediately followed by whitespace containing a new-line character.[^1] [*Note 1*: A new-line character ends the preprocessing directive even if it occurs within what would otherwise be an invocation of a function-like macro. — *end note*]

[*Example 1*:

```cpp
#                       // preprocessing directive
module ;                // preprocessing directive
export module leftpad;  // preprocessing directive
import <string>;        // preprocessing directive
export import "squee";  // preprocessing directive
import rightpad;        // preprocessing directive
import :part;           // preprocessing directive

module                  // not a preprocessing directive
;                       // not a preprocessing directive

export                  // not a preprocessing directive
import                  // not a preprocessing directive
foo;                    // not a preprocessing directive

export                  // not a preprocessing directive
import foo;             // preprocessing directive (ill-formed at phase 7)

import ::               // not a preprocessing directive
import ->               // not a preprocessing directive
```

— *end example*]

**2** A sequence of preprocessing tokens is only a *text-line* if it does not begin with a directive-introducing token. A sequence of preprocessing tokens is only a *conditionally-supported-directive* if it does not begin with any of the directive names appearing after a `#` in the syntax. A *conditionally-supported-directive* is conditionally-supported with implementation-defined semantics.

**3** At the start of phase 4 of translation, the *group* of a *pp-global-module-fragment* shall contain neither a *text-line* nor a *pp-import*.

**4** When in a group that is skipped ([[cpp.cond]](cpp.cond.md)), the directive syntax is relaxed to allow any sequence of preprocessing tokens to occur between the directive name and the following new-line character.

**5** The only white-space characters that shall appear between preprocessing tokens within a preprocessing directive (from just after the directive-introducing token through just before the terminating new-line character) are space and horizontal-tab (including spaces that have replaced comments or possibly other white-space characters in translation phase 3).

**6** The implementation can process and skip sections of source files conditionally, include other source files, import macros from header units, and replace macros. These capabilities are called *preprocessing*, because conceptually they occur before translation of the resulting translation unit.

**7** The preprocessing tokens within a preprocessing directive are not subject to macro expansion unless otherwise stated.

[*Example 2*: In:

```cpp
#define EMPTY
EMPTY   #   include <file.h>
```

the sequence of preprocessing tokens on the second line is *not* a preprocessing directive, because it does not begin with a `#` at the start of translation phase 4, even though it will do so after the macro `EMPTY` has been replaced. — *end example*]

[^1]: Thus, preprocessing directives are commonly called “lines”. These “lines” have no other syntactic significance, as all white space is equivalent except in certain situations during preprocessing (see the `#` character string literal creation operator in [[cpp.stringize]](cpp.stringize.md), for example).
