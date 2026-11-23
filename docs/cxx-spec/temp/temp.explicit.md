# 13.9.2 Explicit instantiation [temp.explicit]

[Contents](../README.md) · up: [13.9 Template instantiation and specialization \[temp.spec\]](temp.spec.md) · prev: [13.9.1 Implicit instantiation \[temp.inst\]](temp.inst.md) · next: [13.9.3 Explicit specialization \[temp.expl.spec\]](temp.expl.spec.md)

**1** A class, function, variable, or member template specialization can be explicitly instantiated from its template. A member function, member class or static data member of a class template can be explicitly instantiated from the member definition associated with its class template.

**2** The syntax for explicit instantiation is:

```bnf
explicit-instantiation:
  'extern'ₒₚₜ 'template' declaration
```

There are two forms of explicit instantiation: an explicit instantiation definition and an explicit instantiation declaration. An explicit instantiation declaration begins with the `extern` keyword.

**3** An explicit instantiation shall not use a *storage-class-specifier* ([[dcl.stc]](../dcl/dcl.stc.md)) other than `thread_local`. An explicit instantiation of a function template, member function of a class template, or variable template shall not use the `inline`, `constexpr`, or `consteval` specifiers. No *attribute-specifier-seq* ([[dcl.attr.grammar]](../dcl/dcl.attr.grammar.md)) shall appertain to an explicit instantiation.

**4** If the explicit instantiation is for a class or member class, the *elaborated-type-specifier* in the *declaration* shall include a *simple-template-id*; otherwise, the *declaration* shall be a *simple-declaration* whose *init-declarator-list* comprises a single *init-declarator* that does not have an *initializer*. If the explicit instantiation is for a function or member function, the *unqualified-id* in the *declarator* shall be either a *template-id* or, where all template arguments can be deduced, a *template-name* or *operator-function-id*. [*Note 1*: The declaration may declare a *qualified-id*, in which case the *unqualified-id* of the *qualified-id* must be a *template-id*. — *end note*] If the explicit instantiation is for a member function, a member class or a static data member of a class template specialization, the name of the class template specialization in the *qualified-id* for the member name shall be a *simple-template-id*. If the explicit instantiation is for a variable template specialization, the *unqualified-id* in the *declarator* shall be a *simple-template-id*. An explicit instantiation shall appear in an enclosing namespace of its template. If the name declared in the explicit instantiation is an unqualified name, the explicit instantiation shall appear in the namespace where its template is declared or, if that namespace is inline ([[namespace.def]](../dcl/namespace.def.md)), any namespace from its enclosing namespace set. [*Note 2*: Regarding qualified names in declarators, see [[dcl.meaning]](../dcl/dcl.meaning.md). — *end note*] [*Example 1*:

```cpp
template<class T> class Array { void mf(); };
template class Array<char>;
template void Array<int>::mf();

template<class T> void sort(Array<T>& v) { /* ... */ }
template void sort(Array<char>&);       // argument is deduced here

namespace N {
  template<class T> void f(T&) { }
}
template void N::f<int>(int&);
```

— *end example*]

**5** A declaration of a function template, a variable template, a member function or static data member of a class template, or a member function template of a class or class template shall precede an explicit instantiation of that entity. A definition of a class template, a member class of a class template, or a member class template of a class or class template shall precede an explicit instantiation of that entity unless the explicit instantiation is preceded by an explicit specialization of the entity with the same template arguments. If the *declaration* of the explicit instantiation names an implicitly-declared special member function ([[special]](../class/special.md)), the program is ill-formed.

**6** The *declaration* in an *explicit-instantiation* and the *declaration* produced by the corresponding substitution into the templated function, variable, or class are two declarations of the same entity. [*Note 3*: These declarations are required to have matching types as specified in [[basic.link]](../basic/basic.link.md), except as specified in [[except.spec]](../except/except.spec.md). [*Example 2*:

```cpp
template<typename T> T var = {};
template float var<float>;      // OK, instantiated variable has type float
template int var<int[16]>[];    // OK, absence of major array bound is permitted
template int *var<int>;         // error: instantiated variable has type int

template<typename T> auto av = T();
template int av<int>;           // OK, variable with type int can be redeclared with type auto

template<typename T> auto f() {}
template void f<int>();         // error: function with deduced return type
                                // redeclared with non-deduced return type ([dcl.spec.auto])
```

— *end example*] — *end note*] Despite its syntactic form, the *declaration* in an *explicit-instantiation* for a variable is not itself a definition and does not conflict with the definition instantiated by an explicit instantiation definition for that variable.

**7** For a given set of template arguments, if an explicit instantiation of a template appears after a declaration of an explicit specialization for that template, the explicit instantiation has no effect. Otherwise, for an explicit instantiation definition, the definition of a function template, a variable template, a member function template, or a member function or static data member of a class template shall be present in every translation unit in which it is explicitly instantiated.

**8** An explicit instantiation of a class, function template, or variable template specialization is placed in the namespace in which the template is defined. An explicit instantiation for a member of a class template is placed in the namespace where the enclosing class template is defined. An explicit instantiation for a member template is placed in the namespace where the enclosing class or class template is defined. [*Example 3*:

```cpp
namespace N {
  template<class T> class Y { void mf() { } };
}

template class Y<int>;          // error: class template Y not visible in the global namespace

using N::Y;
template class Y<int>;          // error: explicit instantiation outside of the namespace of the template

template class N::Y<char*>;             // OK: explicit instantiation in namespace N
template void N::Y<double>::mf();       // OK: explicit instantiation in namespace N
```

— *end example*]

**9** A trailing *template-argument* can be left unspecified in an explicit instantiation of a function template specialization or of a member function template specialization provided it can be deduced from the type of a function parameter ([[temp.deduct]](temp.deduct.md)). [*Example 4*:

```cpp
template<class T> class Array { /* ... */ };
template<class T> void sort(Array<T>& v) { /* ... */ }

// instantiate sort(Array<int>&) -- template-argument deduced
template void sort<>(Array<int>&);
```

— *end example*]

**10** [*Note 4*: An explicit instantiation of a constrained template is required to satisfy that template’s associated constraints ([[temp.constr.decl]](temp.constr.decl.md)). The satisfaction of constraints is determined when forming the template name of an explicit instantiation in which all template arguments are specified ([[temp.names]](temp.names.md)), or, for explicit instantiations of function templates, during template argument deduction ([[temp.deduct.decl]](temp.deduct.decl.md)) when one or more trailing template arguments are left unspecified. — *end note*]

**11** An explicit instantiation that names a class template specialization is also an explicit instantiation of the same kind (declaration or definition) of each of its members (not including members inherited from base classes and members that are templates) that has not been previously explicitly specialized in the translation unit containing the explicit instantiation, provided that the associated constraints, if any, of that member are satisfied by the template arguments of the explicit instantiation ([[temp.constr.decl]](temp.constr.decl.md), [[temp.constr.constr]](temp.constr.constr.md)), except as described below. [*Note 5*: In addition, it will typically be an explicit instantiation of certain implementation-dependent data about the class. — *end note*]

**12** An explicit instantiation definition that names a class template specialization explicitly instantiates the class template specialization and is an explicit instantiation definition of only those members that have been defined at the point of instantiation.

**13** An explicit instantiation of a prospective destructor ([[class.dtor]](../class/class.dtor.md)) shall name the selected destructor of the class.

**14** If an entity is the subject of both an explicit instantiation declaration and an explicit instantiation definition in the same translation unit, the definition shall follow the declaration. An entity that is the subject of an explicit instantiation declaration and that is also used in a way that would otherwise cause an implicit instantiation ([[temp.inst]](temp.inst.md)) in the translation unit shall be the subject of an explicit instantiation definition somewhere in the program; otherwise the program is ill-formed, no diagnostic required. [*Note 6*: This rule does apply to inline functions even though an explicit instantiation declaration of such an entity has no other normative effect. This is needed to ensure that if the address of an inline function is taken in a translation unit in which the implementation chose to suppress the out-of-line body, another translation unit will supply the body. — *end note*] An explicit instantiation declaration shall not name a specialization of a template with internal linkage.

**15** An explicit instantiation does not constitute a use of a default argument, so default argument instantiation is not done. [*Example 5*:

```cpp
char* p = 0;
template<class T> T g(T x = &p) { return x; }
template int g<int>(int);       // OK even though &p isn't an int.
```

— *end example*]
