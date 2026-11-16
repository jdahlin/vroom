# 6.5.3.1 Class members [class.qual]

[Contents](../README.md) · up: [6.5.3 Qualified name lookup \[basic.lookup.qual\]](basic.lookup.qual.md) · prev: [6.5.3 Qualified name lookup \[basic.lookup.qual\]](basic.lookup.qual.md) · next: [6.5.3.2 Namespace members \[namespace.qual\]](namespace.qual.md)

**1** If the *nested-name-specifier* of a *qualified-id* nominates a class, the name specified after the *nested-name-specifier* is looked up in the scope of the class ([[class.member.lookup]](../class/class.member.lookup.md)), except for the cases listed below. The name shall represent one or more members of that class or of one of its base classes ([[class.derived]](../class/class.derived.md)). [*Note 1*: A class member can be referred to using a *qualified-id* at any point in its potential scope ([[basic.scope.class]](basic.scope.class.md)). — *end note*] The exceptions to the name lookup rule above are the following:

- the lookup for a destructor is as specified in [[basic.lookup.qual]](basic.lookup.qual.md);
- a *conversion-type-id* of a *conversion-function-id* is looked up in the same manner as a *conversion-type-id* in a class member access (see [[basic.lookup.classref]](basic.lookup.classref.md));
- the names in a *template-argument* of a *template-id* are looked up in the context in which the entire *postfix-expression* occurs;
- the lookup for a name specified in a *using-declaration* ([[namespace.udecl]](../dcl/namespace.udecl.md)) also finds class or enumeration names hidden within the same scope ([[basic.scope.hiding]](basic.scope.hiding.md)).

**2** In a lookup in which function names are not ignored[^1] and the *nested-name-specifier* nominates a class `C`:

- if the name specified after the *nested-name-specifier*, when looked up in `C`, is the injected-class-name of `C` ([[class.pre]](../class/class.pre.md)), or
- in a *using-declarator* of a *using-declaration* ([[namespace.udecl]](../dcl/namespace.udecl.md)) that is a *member-declaration*, if the name specified after the *nested-name-specifier* is the same as the *identifier* or the *simple-template-id*’s *template-name* in the last component of the *nested-name-specifier*,

the name is instead considered to name the constructor of class `C`. [*Note 2*: For example, the constructor is not an acceptable lookup result in an *elaborated-type-specifier* so the constructor would not be used in place of the injected-class-name. — *end note*] Such a constructor name shall be used only in the *declarator-id* of a declaration that names a constructor or in a *using-declaration*. [*Example 1*:

```cpp
struct A { A(); };
struct B: public A { B(); };

A::A() { }
B::B() { }

B::A ba;            // object of type A
A::A a;             // error: A::A is not a type name
struct A::A a2;     // object of type A
```

— *end example*]

**3** A class member name hidden by a name in a nested declarative region or by the name of a derived class member can still be found if qualified by the name of its class followed by the `::` operator.

[^1]: Lookups in which function names are ignored include names appearing in a *nested-name-specifier*, an *elaborated-type-specifier*, or a *base-specifier*.
