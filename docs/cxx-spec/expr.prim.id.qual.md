# 7.5.4.2 Qualified names [expr.prim.id.qual]

[Contents](README.md) · up: [7.5.4 Names \[expr.prim.id\]](expr.prim.id.md) · prev: [7.5.4.1 Unqualified names \[expr.prim.id.unqual\]](expr.prim.id.unqual.md) · next: [7.5.4.3 Destruction \[expr.prim.id.dtor\]](expr.prim.id.dtor.md)

```bnf
qualified-id:
    nested-name-specifier 'template'ₒₚₜ unqualified-id
```

```bnf
nested-name-specifier:
    '::'
    type-name '::'
    namespace-name '::'
    decltype-specifier '::'
    nested-name-specifier identifier '::'
    nested-name-specifier 'template'ₒₚₜ simple-template-id '::'
```

**1** The type denoted by a *decltype-specifier* in a *nested-name-specifier* shall be a class or enumeration type.

**2** A *nested-name-specifier* that denotes a class, optionally followed by the keyword `template` ([[temp.names]](temp.names.md)), and then followed by the name of a member of either that class ([[class.mem]](class.mem.md)) or one of its base classes ([[class.derived]](class.derived.md)), is a *qualified-id*; [[class.qual]](class.qual.md) describes name lookup for class members that appear in *qualified-id*s. The result is the member. The type of the result is the type of the member. The result is an lvalue if the member is a static member function or a data member and a prvalue otherwise. [*Note 1*: A class member can be referred to using a *qualified-id* at any point in its potential scope ([[basic.scope.class]](basic.scope.class.md)). — *end note*] Where *type-name* `::~` *type-name* is used, the two *type-name*s shall refer to the same type (ignoring cv-qualifications); this notation denotes the destructor of the type so named ([[expr.prim.id.dtor]](expr.prim.id.dtor.md)). The *unqualified-id* in a *qualified-id* shall not be of the form `~`*decltype-specifier*.

**3** The *nested-name-specifier* `::` names the global namespace. A *nested-name-specifier* that names a namespace ([[basic.namespace]](basic.namespace.md)), optionally followed by the keyword `template` ([[temp.names]](temp.names.md)), and then followed by the name of a member of that namespace (or the name of a member of a namespace made visible by a *using-directive*), is a *qualified-id*; [[namespace.qual]](namespace.qual.md) describes name lookup for namespace members that appear in *qualified-id*s. The result is the member. The type of the result is the type of the member. The result is an lvalue if the member is a function, a variable, or a structured binding ([[dcl.struct.bind]](dcl.struct.bind.md)) and a prvalue otherwise.

**4** A *nested-name-specifier* that denotes an enumeration ([[dcl.enum]](dcl.enum.md)), followed by the name of an enumerator of that enumeration, is a *qualified-id* that refers to the enumerator. The result is the enumerator. The type of the result is the type of the enumeration. The result is a prvalue.

**5** In a *qualified-id*, if the *unqualified-id* is a *conversion-function-id*, its *conversion-type-id* is first looked up in the class denoted by the *nested-name-specifier* of the *qualified-id* and the name, if found, is used. Otherwise, it is looked up in the context in which the entire *qualified-id* occurs. In each of these lookups, only names that denote types or templates whose specializations are types are considered.
