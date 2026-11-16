# 11.10 Initialization [class.init]

[Contents](README.md) · up: [11 Classes \[class\]](class.md) · prev: [11.9.7 Nested classes \[class.access.nest\]](class.access.nest.md) · next: [11.10.1 Explicit initialization \[class.expl.init\]](class.expl.init.md)

**1** When no initializer is specified for an object of (possibly cv-qualified) class type (or array thereof), or the initializer has the form `()`, the object is initialized as specified in [[dcl.init]](dcl.init.md).

**2** An object of class type (or array thereof) can be explicitly initialized; see [[class.expl.init]](class.expl.init.md) and [[class.base.init]](class.base.init.md).

**3** When an array of class objects is initialized (either explicitly or implicitly) and the elements are initialized by constructor, the constructor shall be called for each element of the array, following the subscript order; see [[dcl.array]](dcl.array.md). [*Note 1*: Destructors for the array elements are called in reverse order of their construction. — *end note*]

## Subclauses

- [11.10.1 Explicit initialization \[class.expl.init\]](class.expl.init.md)
- [11.10.2 Initializing bases and members \[class.base.init\]](class.base.init.md)
- [11.10.3 Initialization by inherited constructor \[class.inhctor.init\]](class.inhctor.init.md)
- [11.10.4 Construction and destruction \[class.cdtor\]](class.cdtor.md)
- [11.10.5 Copy/move elision \[class.copy.elision\]](class.copy.elision.md)
