# 16.5.5.2 Headers [res.on.headers]

[Contents](../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.1 Overview \[conforming.overview\]](conforming.overview.md) · next: [16.5.5.3 Restrictions on macro definitions \[res.on.macro.definitions\]](res.on.macro.definitions.md)

**1** A C++ header may include other C++ headers. A C++ header shall provide the declarations and definitions that appear in its synopsis. A C++ header shown in its synopsis as including other C++ headers shall provide the declarations and definitions that appear in the synopses of those other headers.

**2** Certain types and macros are defined in more than one header. Every such entity shall be defined such that any header that defines it may be included after any other header that also defines it ([[basic.def.odr]](../basic.def.odr.md)).

**3** The C standard library headers ([[depr.c.headers]](../depr.c.headers.md)) shall include only their corresponding C++ standard library header, as described in [[headers]](headers.md).
