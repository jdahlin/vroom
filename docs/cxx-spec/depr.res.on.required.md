# D.10 Requires paragraph [depr.res.on.required]

[Contents](README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.9.6 Other C headers \[depr.c.headers.other\]](depr.c.headers.other.md) · next: [D.11 Relational operators \[depr.relops\]](depr.relops.md)

**1** In addition to the elements specified in [[structure.specifications]](lib/structure.specifications.md), descriptions of function semantics may also contain a *Requires:* element to denote the preconditions for calling a function.

**2** Violation of any preconditions specified in a function’s *Requires:* element results in undefined behavior unless the function’s *Throws:* element specifies throwing an exception when the precondition is violated.
