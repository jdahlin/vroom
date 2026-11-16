# 13.1 Preamble [temp.pre]

[Contents](../README.md) · up: [13 Templates \[temp\]](temp.md) · prev: [13 Templates \[temp\]](temp.md) · next: [13.2 Template parameters \[temp.param\]](temp.param.md)

**1** A *template* defines a family of classes, functions, or variables, an alias for a family of types, or a concept.

```bnf
template-declaration:
  template-head declaration
  template-head concept-definition
```

```bnf
template-head:
  'template' '<' template-parameter-list '>' requires-clauseₒₚₜ
```

```bnf
template-parameter-list:
  template-parameter
  template-parameter-list ',' template-parameter
```

```bnf
requires-clause:
  'requires' constraint-logical-or-expression
```

```bnf
constraint-logical-or-expression:
  constraint-logical-and-expression
  constraint-logical-or-expression '||' constraint-logical-and-expression
```

```bnf
constraint-logical-and-expression:
  primary-expression
  constraint-logical-and-expression '&&' primary-expression
```

[*Note 1*: The `>` token following the *template-parameter-list* of a *template-declaration* may be the product of replacing a `>>` token by two consecutive `>` tokens ([[temp.names]](temp.names.md)). — *end note*]

**2** The *declaration* in a *template-declaration* (if any) shall

- declare or define a function, a class, or a variable, or
- define a member function, a member class, a member enumeration, or a static data member of a class template or of a class nested within a class template, or
- define a member template of a class or class template, or
- be a *deduction-guide*, or
- be an *alias-declaration*.

**3** A *template-declaration* is a *declaration*. A declaration introduced by a template declaration of a variable is a *variable template*. A variable template at class scope is a *static data member template*.

[*Example 1*:

```cpp
template<class T>
  constexpr T pi = T(3.1415926535897932385L);
template<class T>
  T circular_area(T r) {
    return pi<T> * r * r;
  }
struct matrix_constants {
  template<class T>
    using pauli = hermitian_matrix<T, 2>;
  template<class T>
    constexpr static pauli<T> sigma1 = { { 0, 1 }, { 1, 0 } };
  template<class T>
    constexpr static pauli<T> sigma2 = { { 0, -1i }, { 1i, 0 } };
  template<class T>
    constexpr static pauli<T> sigma3 = { { 1, 0 }, { 0, -1 } };
};
```

— *end example*]

**4** A *template-declaration* can appear only as a namespace scope or class scope declaration. Its *declaration* shall not be an *export-declaration*. In a function template declaration, the last component of the *declarator-id* shall not be a *template-id*. [*Note 2*: That last component may be an *identifier*, an *operator-function-id*, a *conversion-function-id*, or a *literal-operator-id*. In a class template declaration, if the class name is a *simple-template-id*, the declaration declares a class template partial specialization ([[temp.class.spec]](temp.class.spec.md)). — *end note*]

**5** In a *template-declaration*, explicit specialization, or explicit instantiation the *init-declarator-list* in the declaration shall contain at most one declarator. When such a declaration is used to declare a class template, no declarator is permitted.

**6** A template name has linkage ([[basic.link]](../basic/basic.link.md)). Specializations (explicit or implicit) of a template that has internal linkage are distinct from all specializations in other translation units. A template, a template explicit specialization ([[temp.expl.spec]](temp.expl.spec.md)), and a class template partial specialization shall not have C linkage. Use of a linkage specification other than `"C"` or `"C++"` with any of these constructs is conditionally-supported, with implementation-defined semantics. Template definitions shall obey the one-definition rule ([[basic.def.odr]](../basic/basic.def.odr.md)). [*Note 3*: Default arguments for function templates and for member functions of class templates are considered definitions for the purpose of template instantiation ([[temp.decls]](temp.decls.md)) and must also obey the one-definition rule. — *end note*]

**7** A class template shall not have the same name as any other template, class, function, variable, enumeration, enumerator, namespace, or type in the same scope ([[basic.scope]](../basic/basic.scope.md)), except as specified in [[temp.class.spec]](temp.class.spec.md). Except that a function template can be overloaded either by non-template functions ([[dcl.fct]](../dcl/dcl.fct.md)) with the same name or by other function templates with the same name ([[temp.over]](temp.over.md)), a template name declared in namespace scope or in class scope shall be unique in that scope.

**8** An entity is *templated* if it is

- a template,
- an entity defined ([[basic.def]](../basic/basic.def.md)) or created ([[class.temporary]](../basic/class.temporary.md)) in a templated entity,
- a member of a templated entity,
- an enumerator for an enumeration that is a templated entity, or
- the closure type of a *lambda-expression* ([[expr.prim.lambda.closure]](../expr/expr.prim.lambda.closure.md)) appearing in the declaration of a templated entity.

[*Note 4*: A local class, a local variable, or a friend function defined in a templated entity is a templated entity. — *end note*]

**9** A *template-declaration* is written in terms of its template parameters. The optional *requires-clause* following a *template-parameter-list* allows the specification of constraints ([[temp.constr.decl]](temp.constr.decl.md)) on template arguments ([[temp.arg]](temp.arg.md)). The *requires-clause* introduces the *constraint-expression* that results from interpreting the *constraint-logical-or-expression* as a *constraint-expression*. The *constraint-logical-or-expression* of a *requires-clause* is an unevaluated operand ([[expr.context]](../expr/expr.context.md)). [*Note 5*: The expression in a *requires-clause* uses a restricted grammar to avoid ambiguities. Parentheses can be used to specify arbitrary expressions in a *requires-clause*. [*Example 2*:

```cpp
template<int N> requires N == sizeof new unsigned short
int f();            // error: parentheses required around == expression
```

— *end example*] — *end note*]

**10** A definition of a function template, member function of a class template, variable template, or static data member of a class template shall be reachable from the end of every definition domain ([[basic.def.odr]](../basic/basic.def.odr.md)) in which it is implicitly instantiated ([[temp.inst]](temp.inst.md)) unless the corresponding specialization is explicitly instantiated ([[temp.explicit]](temp.explicit.md)) in some translation unit; no diagnostic is required.
