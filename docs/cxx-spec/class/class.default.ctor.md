# 11.4.4.1 Default constructors [class.default.ctor]

[Contents](../README.md) · up: [11.4.4 Constructors \[class.ctor\]](class.ctor.md) · prev: [11.4.4 Constructors \[class.ctor\]](class.ctor.md) · next: [11.4.4.2 Copy/move constructors \[class.copy.ctor\]](class.copy.ctor.md)

**1** A *default constructor* for a class `X` is a constructor of class `X` for which each parameter that is not a function parameter pack has a default argument (including the case of a constructor with no parameters). If there is no user-declared constructor for class `X`, a non-explicit constructor having no parameters is implicitly declared as defaulted ([[dcl.fct.def]](../dcl/dcl.fct.def.md)). An implicitly-declared default constructor is an inline public member of its class.

**2** A defaulted default constructor for class `X` is defined as deleted if:

- `X` is a union that has a variant member with a non-trivial default constructor and no variant member of `X` has a default member initializer,
- `X` is a non-union class that has a variant member `M` with a non-trivial default constructor and no variant member of the anonymous union containing `M` has a default member initializer,
- any non-static data member with no default member initializer ([[class.mem]](class.mem.md)) is of reference type,
- any non-variant non-static data member of const-qualified type (or array thereof) with no *brace-or-equal-initializer* is not const-default-constructible ([[dcl.init]](../dcl/dcl.init.md)),
- `X` is a union and all of its variant members are of const-qualified type (or array thereof),
- `X` is a non-union class and all members of any anonymous union member are of const-qualified type (or array thereof),
- any potentially constructed subobject, except for a non-static data member with a *brace-or-equal-initializer*, has class type `M` (or array thereof) and either `M` has no default constructor or overload resolution ([[over.match]](../over/over.match.md)) as applied to find `M`’s corresponding constructor results in an ambiguity or in a function that is deleted or inaccessible from the defaulted default constructor, or
- any potentially constructed subobject has a type with a destructor that is deleted or inaccessible from the defaulted default constructor.

**3** A default constructor is *trivial* if it is not user-provided and if:

- its class has no virtual functions ([[class.virtual]](class.virtual.md)) and no virtual base classes ([[class.mi]](class.mi.md)), and
- no non-static data member of its class has a default member initializer ([[class.mem]](class.mem.md)), and
- all the direct base classes of its class have trivial default constructors, and
- for all the non-static data members of its class that are of class type (or array thereof), each such class has a trivial default constructor.

Otherwise, the default constructor is *non-trivial*.

**4** A default constructor that is defaulted and not defined as deleted is *implicitly defined* when it is odr-used ([[basic.def.odr]](../basic/basic.def.odr.md)) to create an object of its class type ([[intro.object]](../basic/intro.object.md)), when it is needed for constant evaluation ([[expr.const]](../expr/expr.const.md)), or when it is explicitly defaulted after its first declaration. The implicitly-defined default constructor performs the set of initializations of the class that would be performed by a user-written default constructor for that class with no *ctor-initializer* ([[class.base.init]](class.base.init.md)) and an empty *compound-statement*. If that user-written default constructor would be ill-formed, the program is ill-formed. If that user-written default constructor would satisfy the requirements of a constexpr constructor ([[dcl.constexpr]](../dcl/dcl.constexpr.md)), the implicitly-defined default constructor is `constexpr`. Before the defaulted default constructor for a class is implicitly defined, all the non-user-provided default constructors for its base classes and its non-static data members are implicitly defined. [*Note 1*: An implicitly-declared default constructor has an exception specification ([[except.spec]](../except/except.spec.md)). An explicitly-defaulted definition might have an implicit exception specification, see [[dcl.fct.def]](../dcl/dcl.fct.def.md). — *end note*]

**5** Default constructors are called implicitly to create class objects of static, thread, or automatic storage duration ([[basic.stc.static]](../basic/basic.stc.static.md), [[basic.stc.thread]](../basic/basic.stc.thread.md), [[basic.stc.auto]](../basic/basic.stc.auto.md)) defined without an initializer ([[dcl.init]](../dcl/dcl.init.md)), are called to create class objects of dynamic storage duration ([[basic.stc.dynamic]](../basic/basic.stc.dynamic.md)) created by a *new-expression* in which the *new-initializer* is omitted ([[expr.new]](../expr/expr.new.md)), or are called when the explicit type conversion syntax ([[expr.type.conv]](../expr/expr.type.conv.md)) is used. A program is ill-formed if the default constructor for an object is implicitly used and the constructor is not accessible ([[class.access]](class.access.md)).

**6** [*Note 2*: [[class.base.init]](class.base.init.md) describes the order in which constructors for base classes and non-static data members are called and describes how arguments can be specified for the calls to these constructors. — *end note*]
