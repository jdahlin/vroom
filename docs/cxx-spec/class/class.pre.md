# 11.1 Preamble [class.pre]

[Contents](../README.md) · up: [11 Classes \[class\]](class.md) · prev: [11 Classes \[class\]](class.md) · next: [11.2 Properties of classes \[class.prop\]](class.prop.md)

**1** A class is a type. Its name becomes a *class-name* ([[class.name]](class.name.md)) within its scope.

```bnf
class-name:
    identifier
    simple-template-id
```

A *class-specifier* or an *elaborated-type-specifier* ([[dcl.type.elab]](../dcl/dcl.type.elab.md)) is used to make a *class-name*. An object of a class consists of a (possibly empty) sequence of members and base class objects.

```bnf
class-specifier:
    class-head '{' member-specificationₒₚₜ '}'
```

```bnf
class-head:
    class-key attribute-specifier-seqₒₚₜ class-head-name class-virt-specifierₒₚₜ base-clauseₒₚₜ
    class-key attribute-specifier-seqₒₚₜ base-clauseₒₚₜ
```

```bnf
class-head-name:
    nested-name-specifierₒₚₜ class-name
```

```bnf
class-virt-specifier:
    'final'
```

```bnf
class-key:
    'class'
    'struct'
    'union'
```

A class declaration where the *class-name* in the *class-head-name* is a *simple-template-id* shall be an explicit specialization ([[temp.expl.spec]](../temp/temp.expl.spec.md)) or a partial specialization ([[temp.class.spec]](../temp/temp.class.spec.md)). A *class-specifier* whose *class-head* omits the *class-head-name* defines an unnamed class. [*Note 1*: An unnamed class thus can’t be `final`. — *end note*]

**2** A *class-name* is inserted into the scope in which it is declared immediately after the *class-name* is seen. The *class-name* is also inserted into the scope of the class itself; this is known as the *injected-class-name*. For purposes of access checking, the injected-class-name is treated as if it were a public member name. A *class-specifier* is commonly referred to as a *class definition*. A class is considered defined after the closing brace of its *class-specifier* has been seen even though its member functions are in general not yet defined. The optional *attribute-specifier-seq* appertains to the class; the attributes in the *attribute-specifier-seq* are thereafter considered attributes of the class whenever it is named.

**3** If a *class-head-name* contains a *nested-name-specifier*, the *class-specifier* shall refer to a class that was previously declared directly in the class or namespace to which the *nested-name-specifier* refers, or in an element of the inline namespace set ([[namespace.def]](../dcl/namespace.def.md)) of that namespace (i.e., not merely inherited or introduced by a *using-declaration*), and the *class-specifier* shall appear in a namespace enclosing the previous declaration. In such cases, the *nested-name-specifier* of the *class-head-name* of the definition shall not begin with a *decltype-specifier*.

**4** [*Note 2*: The *class-key* determines whether the class is a union ([[class.union]](class.union.md)) and whether access is public or private by default ([[class.access]](class.access.md)). A union holds the value of at most one data member at a time. — *end note*]

**5** If a class is marked with the *class-virt-specifier* `final` and it appears as a *class-or-decltype* in a *base-clause* ([[class.derived]](class.derived.md)), the program is ill-formed. Whenever a *class-key* is followed by a *class-head-name*, the *identifier* `final`, and a colon or left brace, `final` is interpreted as a *class-virt-specifier*. [*Example 1*:

```cpp
struct A;
struct A final {};      // OK: definition of struct A,
                        // not value-initialization of variable final

struct X {
 struct C { constexpr operator int() { return 5; } };
 struct B final : C{};  // OK: definition of nested class B,
                        // not declaration of a bit-field member final
};
```

— *end example*]

**6** [*Note 3*: Complete objects of class type have nonzero size. Base class subobjects and members declared with the `no_unique_address` attribute ([[dcl.attr.nouniqueaddr]](../dcl/dcl.attr.nouniqueaddr.md)) are not so constrained. — *end note*]

**7** [*Note 4*: Class objects can be assigned ([[over.ass]](../over/over.ass.md), [[class.copy.assign]](class.copy.assign.md)), passed as arguments to functions ([[dcl.init]](../dcl/dcl.init.md), [[class.copy.ctor]](class.copy.ctor.md)), and returned by functions (except objects of classes for which copying or moving has been restricted; see [[dcl.fct.def.delete]](../dcl/dcl.fct.def.delete.md) and [[class.access]](class.access.md)). Other plausible operators, such as equality comparison, can be defined by the user; see [[over.oper]](../over/over.oper.md). — *end note*]
