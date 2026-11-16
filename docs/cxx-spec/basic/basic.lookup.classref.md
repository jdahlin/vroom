# 6.5.5 Class member access [basic.lookup.classref]

[Contents](../README.md) · up: [6.5 Name lookup \[basic.lookup\]](basic.lookup.md) · prev: [6.5.4 Elaborated type specifiers \[basic.lookup.elab\]](basic.lookup.elab.md) · next: [6.5.6 Using-directives and namespace aliases \[basic.lookup.udir\]](basic.lookup.udir.md)

**1** In a class member access expression ([[expr.ref]](../expr/expr.ref.md)), if the `.` or `->` token is immediately followed by an *identifier* followed by a `<`, the identifier must be looked up to determine whether the `<` is the beginning of a template argument list ([[temp.names]](../temp/temp.names.md)) or a less-than operator. The identifier is first looked up in the class of the object expression ([[class.member.lookup]](../class/class.member.lookup.md)). If the identifier is not found, it is then looked up in the context of the entire *postfix-expression* and shall name a template whose specializations are types.

**2** If the *id-expression* in a class member access ([[expr.ref]](../expr/expr.ref.md)) is an *unqualified-id*, and the type of the object expression is of a class type `C`, the *unqualified-id* is looked up in the scope of class `C` ([[class.member.lookup]](../class/class.member.lookup.md)).

**3** If the *unqualified-id* is `~`*type-name*, the *type-name* is looked up in the context of the entire *postfix-expression*. If the type `T` of the object expression is of a class type `C`, the *type-name* is also looked up in the scope of class `C`. At least one of the lookups shall find a name that refers to *cv* `T`. [*Example 1*:

```cpp
struct A { };

struct B {
  struct A { };
  void f(::A* a);
};

void B::f(::A* a) {
  a->~A();                      // OK: lookup in *a finds the injected-class-name
}
```

— *end example*]

**4** If the *id-expression* in a class member access is a *qualified-id* of the form

```cpp
class-name-or-namespace-name::...
```

the `class-name-or-namespace-name` following the `.` or `->` operator is first looked up in the class of the object expression ([[class.member.lookup]](../class/class.member.lookup.md)) and the name, if found, is used. Otherwise it is looked up in the context of the entire *postfix-expression*. [*Note 1*: See [[basic.lookup.qual]](basic.lookup.qual.md), which describes the lookup of a name before `::`, which will only find a type or namespace name. — *end note*]

**5** If the *qualified-id* has the form

```cpp
::class-name-or-namespace-name::...
```

the `class-name-or-namespace-name` is looked up in global scope as a *class-name* or *namespace-name*.

**6** If the *nested-name-specifier* contains a *simple-template-id* ([[temp.names]](../temp/temp.names.md)), the names in its *template-argument*s are looked up in the context in which the entire *postfix-expression* occurs.

**7** If the *id-expression* is a *conversion-function-id*, its *conversion-type-id* is first looked up in the class of the object expression ([[class.member.lookup]](../class/class.member.lookup.md)) and the name, if found, is used. Otherwise it is looked up in the context of the entire *postfix-expression*. In each of these lookups, only names that denote types or templates whose specializations are types are considered. [*Example 2*:

```cpp
struct A { };
namespace N {
  struct A {
    void g() { }
    template <class T> operator T();
  };
}

int main() {
  N::A a;
  a.operator A();               // calls N::A::operator N::A
}
```

— *end example*]
