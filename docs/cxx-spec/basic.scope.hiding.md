# 6.4.10 Name hiding [basic.scope.hiding]

[Contents](README.md) · up: [6.4 Scope \[basic.scope\]](basic.scope.md) · prev: [6.4.9 Template parameter scope \[basic.scope.temp\]](basic.scope.temp.md) · next: [6.5 Name lookup \[basic.lookup\]](basic.lookup.md)

**1** A declaration of a name in a nested declarative region hides a declaration of the same name in an enclosing declarative region; see [[basic.scope.declarative]](basic.scope.declarative.md) and [[basic.lookup.unqual]](basic.lookup.unqual.md).

**2** If a class name ([[class.name]](class.name.md)) or enumeration name ([[dcl.enum]](dcl.enum.md)) and a variable, data member, function, or enumerator are declared in the same declarative region (in any order) with the same name (excluding declarations made visible via *using-directive*s ([[basic.lookup.unqual]](basic.lookup.unqual.md))), the class or enumeration name is hidden wherever the variable, data member, function, or enumerator name is visible.

**3** In a member function definition, the declaration of a name at block scope hides the declaration of a member of the class with the same name; see [[basic.scope.class]](basic.scope.class.md). The declaration of a member in a derived class ([[class.derived]](class.derived.md)) hides the declaration of a member of a base class of the same name; see [[class.member.lookup]](class.member.lookup.md).

**4** During the lookup of a name qualified by a namespace name, declarations that would otherwise be made visible by a *using-directive* can be hidden by declarations with the same name in the namespace containing the *using-directive*; see [[namespace.qual]](namespace.qual.md).

**5** If a name is in scope and is not hidden it is said to be *visible*.
