# D.8 Implicit declaration of copy functions [depr.impldec]

[Contents](README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.7 Non-local use of TU-local entities \[depr.local\]](depr.local.md) · next: [D.9 C headers \[depr.c.headers\]](depr.c.headers.md)

**1** The implicit definition of a copy constructor ([[class.copy.ctor]](class.copy.ctor.md)) as defaulted is deprecated if the class has a user-declared copy assignment operator or a user-declared destructor ([[class.dtor]](class.dtor.md)). The implicit definition of a copy assignment operator ([[class.copy.assign]](class.copy.assign.md)) as defaulted is deprecated if the class has a user-declared copy constructor or a user-declared destructor. In a future revision of this International Standard, these implicit definitions could become deleted ([[dcl.fct.def.delete]](dcl.fct.def.delete.md)).
