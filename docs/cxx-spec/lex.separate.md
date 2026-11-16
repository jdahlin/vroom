# 5.1 Separate translation [lex.separate]

[Contents](README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5 Lexical conventions \[lex\]](lex.md) · next: [5.2 Phases of translation \[lex.phases\]](lex.phases.md)

**1** The text of the program is kept in units called *source files* in this document. A source file together with all the headers ([[headers]](lib/headers.md)) and source files included ([[cpp.include]](cpp.include.md)) via the preprocessing directive `#include`, less any source lines skipped by any of the conditional inclusion ([[cpp.cond]](cpp.cond.md)) preprocessing directives, is called a *translation unit*. [*Note 1*: A C++ program need not all be translated at the same time. — *end note*]

**2** [*Note 2*: Previously translated translation units and instantiation units can be preserved individually or in libraries. The separate translation units of a program communicate ([[basic.link]](basic.link.md)) by (for example) calls to functions whose identifiers have external or module linkage, manipulation of objects whose identifiers have external or module linkage, or manipulation of data files. Translation units can be separately translated and then later linked to produce an executable program ([[basic.link]](basic.link.md)). — *end note*]
