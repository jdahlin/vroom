# 4.1 Implementation compliance [intro.compliance]

[Contents](README.md) · up: [4 General principles \[intro\]](intro.md) · prev: [4 General principles \[intro\]](intro.md) · next: [4.1.1 Abstract machine \[intro.abstract\]](intro.abstract.md)

**1** The set of *diagnosable rules* consists of all syntactic and semantic rules in this document except for those rules containing an explicit notation that “no diagnostic is required” or which are described as resulting in “undefined behavior”.

**2** Although this document states only requirements on C++ implementations, those requirements are often easier to understand if they are phrased as requirements on programs, parts of programs, or execution of programs. Such requirements have the following meaning:

- If a program contains no violations of the rules in this document, a conforming implementation shall, within its resource limits, accept and correctly execute[^1] that program.
- If a program contains a violation of any diagnosable rule or an occurrence of a construct described in this document as “conditionally-supported” when the implementation does not support that construct, a conforming implementation shall issue at least one diagnostic message.
- If a program contains a violation of a rule for which no diagnostic is required, this document places no requirement on implementations with respect to that program.

[*Note 1*: During template argument deduction and substitution, certain constructs that in other contexts require a diagnostic are treated differently; see [[temp.deduct]](temp.deduct.md). — *end note*]

**3** For classes and class templates, the library Clauses specify partial definitions. Private members ([[class.access]](class.access.md)) are not specified, but each implementation shall supply them to complete the definitions according to the description in the library Clauses.

**4** For functions, function templates, objects, and values, the library Clauses specify declarations. Implementations shall supply definitions consistent with the descriptions in the library Clauses.

**5** The names defined in the library have namespace scope ([[basic.namespace]](basic.namespace.md)). A C++ translation unit ([[lex.phases]](lex.phases.md)) obtains access to these names by including the appropriate standard library header or importing the appropriate standard library named header unit ([[using.headers]](lib/using.headers.md)).

**6** The templates, classes, functions, and objects in the library have external linkage ([[basic.link]](basic.link.md)). The implementation provides definitions for standard library entities, as necessary, while combining translation units to form a complete C++ program ([[lex.phases]](lex.phases.md)).

**7** Two kinds of implementations are defined: a *hosted implementation* and a *freestanding implementation*. For a hosted implementation, this document defines the set of available libraries. A freestanding implementation is one in which execution may take place without the benefit of an operating system, and has an implementation-defined set of libraries that includes certain language-support libraries ([[compliance]](lib/compliance.md)).

**8** A conforming implementation may have extensions (including additional library functions), provided they do not alter the behavior of any well-formed program. Implementations are required to diagnose programs that use such extensions that are ill-formed according to this document. Having done so, however, they can compile and execute such programs.

**9** Each implementation shall include documentation that identifies all conditionally-supported constructs that it does not support and defines all locale-specific characteristics.[^2]

## Subclauses

- [4.1.1 Abstract machine \[intro.abstract\]](intro.abstract.md)

[^1]: “Correct execution” can include undefined behavior, depending on the data being processed; see [[intro.defs]](intro.defs.md) and [[intro.execution]](intro.execution.md).
[^2]: This documentation also defines implementation-defined behavior; see [[intro.abstract]](intro.abstract.md).
