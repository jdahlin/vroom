# A.1 Keywords [gram.key]

[Contents](../README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [Annex A Grammar summary \[gram\]](gram.md) · next: [A.2 Lexical conventions \[gram.lex\]](gram.lex.md)

**1** New context-dependent keywords are introduced into a program by `typedef` ([[dcl.typedef]](../dcl/dcl.typedef.md)), `namespace` ([[namespace.def]](../dcl/namespace.def.md)), class ([[class]](../class/class.md)), enumeration ([[dcl.enum]](../dcl/dcl.enum.md)), and `template` ([[temp]](../temp/temp.md)) declarations.

```bnf
typedef-name:
    identifier
    simple-template-id
```

```bnf
namespace-name:
    identifier
    namespace-alias

namespace-alias:
    identifier
```

```bnf
class-name:
    identifier
    simple-template-id
```

```bnf
enum-name:
    identifier
```

```bnf
template-name:
    identifier
```
