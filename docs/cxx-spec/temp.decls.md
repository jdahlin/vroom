# 13.7 Template declarations [temp.decls]

[Contents](README.md) · up: [13 Templates \[temp\]](temp.md) · prev: [13.6 Type equivalence \[temp.type\]](temp.type.md) · next: [13.7.1 Class templates \[temp.class\]](temp.class.md)

**1** A *template-id*, that is, the *template-name* followed by a *template-argument-list* shall not be specified in the declaration of a primary template declaration. [*Example 1*:

```cpp
template<class T1, class T2, int I> class A<T1, T2, I> { };     // error
template<class T1, int I> void sort<T1, I>(T1 data[I]);         // error
```

— *end example*] [*Note 1*: However, this syntax is allowed in class template partial specializations ([[temp.class.spec]](temp.class.spec.md)). — *end note*]

**2** For purposes of name lookup and instantiation, default arguments, *type-constraint*s, *requires-clause*s ([[temp.pre]](temp.pre.md)), and *noexcept-specifier*s of function templates and of member functions of class templates are considered definitions; each default argument, *type-constraint*, *requires-clause*, or *noexcept-specifier* is a separate definition which is unrelated to the templated function definition or to any other default arguments *type-constraint*s, *requires-clause*s, or *noexcept-specifier*s. For the purpose of instantiation, the substatements of a constexpr if statement ([[stmt.if]](stmt.if.md)) are considered definitions.

**3** Because an *alias-declaration* cannot declare a *template-id*, it is not possible to partially or explicitly specialize an alias template.

## Subclauses

- [13.7.1 Class templates \[temp.class\]](temp.class.md)
  - [13.7.1.1 Member functions of class templates \[temp.mem.func\]](temp.mem.func.md)
  - [13.7.1.2 Deduction guides \[temp.deduct.guide\]](temp.deduct.guide.md)
  - [13.7.1.3 Member classes of class templates \[temp.mem.class\]](temp.mem.class.md)
  - [13.7.1.4 Static data members of class templates \[temp.static\]](temp.static.md)
  - [13.7.1.5 Enumeration members of class templates \[temp.mem.enum\]](temp.mem.enum.md)
- [13.7.2 Member templates \[temp.mem\]](temp.mem.md)
- [13.7.3 Variadic templates \[temp.variadic\]](temp.variadic.md)
- [13.7.4 Friends \[temp.friend\]](temp.friend.md)
- [13.7.5 Class template partial specializations \[temp.class.spec\]](temp.class.spec.md)
  - [13.7.5.1 Matching of class template partial specializations \[temp.class.spec.match\]](temp.class.spec.match.md)
  - [13.7.5.2 Partial ordering of class template specializations \[temp.class.order\]](temp.class.order.md)
  - [13.7.5.3 Members of class template specializations \[temp.class.spec.mfunc\]](temp.class.spec.mfunc.md)
- [13.7.6 Function templates \[temp.fct\]](temp.fct.md)
  - [13.7.6.1 Function template overloading \[temp.over.link\]](temp.over.link.md)
  - [13.7.6.2 Partial ordering of function templates \[temp.func.order\]](temp.func.order.md)
- [13.7.7 Alias templates \[temp.alias\]](temp.alias.md)
- [13.7.8 Concept definitions \[temp.concept\]](temp.concept.md)
