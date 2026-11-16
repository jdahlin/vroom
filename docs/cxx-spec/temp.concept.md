# 13.7.8 Concept definitions [temp.concept]

[Contents](README.md) · up: [13.7 Template declarations \[temp.decls\]](temp.decls.md) · prev: [13.7.7 Alias templates \[temp.alias\]](temp.alias.md) · next: [13.8 Name resolution \[temp.res\]](temp.res.md)

**1** A *concept* is a template that defines constraints on its template arguments.

```bnf
concept-definition:
  'concept' concept-name '=' constraint-expression ';'
```

```bnf
concept-name:
  identifier
```

**2** A *concept-definition* declares a concept. Its *identifier* becomes a *concept-name* referring to that concept within its scope. [*Example 1*:

```cpp
template<typename T>
concept C = requires(T x) {
  { x == x } -> std::convertible_to<bool>;
};

template<typename T>
  requires C<T>     // C constrains f1(T) in constraint-expression
T f1(T x) { return x; }

template<C T>       // C, as a type-constraint, constrains f2(T)
T f2(T x) { return x; }
```

— *end example*]

**3** A *concept-definition* shall appear at namespace scope ([[basic.scope.namespace]](basic.scope.namespace.md)).

**4** A concept shall not have associated constraints ([[temp.constr.decl]](temp.constr.decl.md)).

**5** A concept is not instantiated ([[temp.spec]](temp.spec.md)). [*Note 1*: A concept-id ([[temp.names]](temp.names.md)) is evaluated as an expression. A concept cannot be explicitly instantiated ([[temp.explicit]](temp.explicit.md)), explicitly specialized ([[temp.expl.spec]](temp.expl.spec.md)), or partially specialized. — *end note*]

**6** The *constraint-expression* of a *concept-definition* is an unevaluated operand ([[expr.context]](expr.context.md)).

**7** The first declared template parameter of a concept definition is its *prototype parameter*. A *type concept* is a concept whose prototype parameter is a type *template-parameter*.
