# 7.5.7 Requires expressions [expr.prim.req]

[Contents](README.md) · up: [7.5 Primary expressions \[expr.prim\]](expr.prim.md) · prev: [7.5.6 Fold expressions \[expr.prim.fold\]](expr.prim.fold.md) · next: [7.5.7.1 Simple requirements \[expr.prim.req.simple\]](expr.prim.req.simple.md)

**1** A *requires-expression* provides a concise way to express requirements on template arguments that can be checked by name lookup ([[basic.lookup]](basic.lookup.md)) or by checking properties of types and expressions.

```bnf
requires-expression:
    'requires' requirement-parameter-listₒₚₜ requirement-body
```

```bnf
requirement-parameter-list:
    '(' parameter-declaration-clauseₒₚₜ ')'
```

```bnf
requirement-body:
    '{' requirement-seq '}'
```

```bnf
requirement-seq:
    requirement
    requirement-seq requirement
```

```bnf
requirement:
    simple-requirement
    type-requirement
    compound-requirement
    nested-requirement
```

**2** A *requires-expression* is a prvalue of type `bool` whose value is described below. Expressions appearing within a *requirement-body* are unevaluated operands ([[expr.prop]](expr.prop.md)).

**3** [*Example 1*: A common use of *requires-expression*s is to define requirements in concepts such as the one below:

```cpp
template<typename T>
  concept R = requires (T i) {
    typename T::type;
    {*i} -> std::convertible_to<const typename T::type&>;
  };
```

A *requires-expression* can also be used in a *requires-clause* ([[temp.pre]](temp.pre.md)) as a way of writing ad hoc constraints on template arguments such as the one below:

```cpp
template<typename T>
  requires requires (T x) { x + x; }
    T add(T a, T b) { return a + b; }
```

The first `requires` introduces the *requires-clause*, and the second introduces the *requires-expression*. — *end example*]

**4** A *requires-expression* may introduce local parameters using a *parameter-declaration-clause* ([[dcl.fct]](dcl.fct.md)). A local parameter of a *requires-expression* shall not have a default argument. Each name introduced by a local parameter is in scope from the point of its declaration until the closing brace of the *requirement-body*. These parameters have no linkage, storage, or lifetime; they are only used as notation for the purpose of defining *requirement*s. The *parameter-declaration-clause* of a *requirement-parameter-list* shall not terminate with an ellipsis. [*Example 2*:

```cpp
template<typename T>
concept C = requires(T t, ...) {    // error: terminates with an ellipsis
  t;
};
```

— *end example*]

**5** The *requirement-body* contains a sequence of *requirement*s. These *requirement*s may refer to local parameters, template parameters, and any other declarations visible from the enclosing context.

**6** The substitution of template arguments into a *requires-expression* may result in the formation of invalid types or expressions in its *requirement*s or the violation of the semantic constraints of those *requirement*s. In such cases, the *requires-expression* evaluates to `false`; it does not cause the program to be ill-formed. The substitution and semantic constraint checking proceeds in lexical order and stops when a condition that determines the result of the *requires-expression* is encountered. If substitution (if any) and semantic constraint checking succeed, the *requires-expression* evaluates to `true`. [*Note 1*: If a *requires-expression* contains invalid types or expressions in its *requirement*s, and it does not appear within the declaration of a templated entity, then the program is ill-formed. — *end note*] If the substitution of template arguments into a *requirement* would always result in a substitution failure, the program is ill-formed; no diagnostic required. [*Example 3*:

```cpp
template<typename T> concept C =
requires {
  new int[-(int)sizeof(T)];     // ill-formed, no diagnostic required
};
```

— *end example*]

## Subclauses

- [7.5.7.1 Simple requirements \[expr.prim.req.simple\]](expr.prim.req.simple.md)
- [7.5.7.2 Type requirements \[expr.prim.req.type\]](expr.prim.req.type.md)
- [7.5.7.3 Compound requirements \[expr.prim.req.compound\]](expr.prim.req.compound.md)
- [7.5.7.4 Nested requirements \[expr.prim.req.nested\]](expr.prim.req.nested.md)
