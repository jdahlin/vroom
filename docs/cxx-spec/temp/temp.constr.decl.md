# 13.5.2 Constrained declarations [temp.constr.decl]

[Contents](../README.md) · up: [13.5 Template constraints \[temp.constr\]](temp.constr.md) · prev: [13.5.1.2 Atomic constraints \[temp.constr.atomic\]](temp.constr.atomic.md) · next: [13.5.3 Constraint normalization \[temp.constr.normal\]](temp.constr.normal.md)

**1** A template declaration ([[temp.pre]](temp.pre.md)) or templated function declaration ([[dcl.fct]](../dcl/dcl.fct.md)) can be constrained by the use of a *requires-clause*. This allows the specification of constraints for that declaration as an expression:

```bnf
constraint-expression:
    logical-or-expression
```

**2** Constraints can also be associated with a declaration through the use of *type-constraint*s in a *template-parameter-list* or parameter-type-list. Each of these forms introduces additional *constraint-expression*s that are used to constrain the declaration.

**3** A declaration’s *associated constraints* are defined as follows:

- If there are no introduced *constraint-expression*s, the declaration has no associated constraints.

- Otherwise, if there is a single introduced *constraint-expression*, the associated constraints are the normal form ([[temp.constr.normal]](temp.constr.normal.md)) of that expression.

- Otherwise, the associated constraints are the normal form of a logical AND expression ([[expr.log.and]](../expr/expr.log.and.md)) whose operands are in the following order:

  - the *constraint-expression* introduced by each *type-constraint* ([[temp.param]](temp.param.md)) in the declaration’s *template-parameter-list*, in order of appearance, and
  - the *constraint-expression* introduced by a *requires-clause* following a *template-parameter-list* ([[temp.pre]](temp.pre.md)), and
  - the *constraint-expression* introduced by each *type-constraint* in the parameter-type-list of a function declaration, and
  - the *constraint-expression* introduced by a trailing *requires-clause* ([[dcl.decl]](../dcl/dcl.decl.md)) of a function declaration ([[dcl.fct]](../dcl/dcl.fct.md)).

The formation of the associated constraints establishes the order in which constraints are instantiated when checking for satisfaction ([[temp.constr.constr]](temp.constr.constr.md)). [*Example 1*:

```cpp
template<typename T> concept C = true;

template<C T> void f1(T);
template<typename T> requires C<T> void f2(T);
template<typename T> void f3(T) requires C<T>;
```

The functions `f1`, `f2`, and `f3` have the associated constraint `C<T>`.

```cpp
template<typename T> concept C1 = true;
template<typename T> concept C2 = sizeof(T) > 0;

template<C1 T> void f4(T) requires C2<T>;
template<typename T> requires C1<T> && C2<T> void f5(T);
```

The associated constraints of `f4` and `f5` are `C1<T> ∧ C2<T>`.

```cpp
template<C1 T> requires C2<T> void f6();
template<C2 T> requires C1<T> void f7();
```

The associated constraints of `f6` are `C1<T> ∧ C2<T>`, and those of `f7` are `C2<T> ∧ C1<T>`. — *end example*]

**4** When determining whether a given introduced *constraint-expression* *C*<sub>1</sub> of a declaration in an instantiated specialization of a templated class is equivalent ([[temp.over.link]](temp.over.link.md)) to the corresponding *constraint-expression* *C*<sub>2</sub> of a declaration outside the class body, *C*<sub>1</sub> is instantiated. If the instantiation results in an invalid expression, the *constraint-expression*s are not equivalent. [*Note 1*: This can happen when determining which member template is specialized by an explicit specialization declaration. — *end note*] [*Example 2*:

```cpp
template <class T> concept C = true;
template <class T> struct A {
  template <class U> U f(U) requires C<typename T::type>;   // #1
  template <class U> U f(U) requires C<T>;                  // #2
};

template <> template <class U>
U A<int>::f(U u) requires C<int> { return u; }              // OK, specializes #2
```

Substituting `int` for `T` in `C<typename T::type>` produces an invalid expression, so the specialization does not match #1. Substituting `int` for `T` in `C<T>` produces `C<int>`, which is equivalent to the *constraint-expression* for the specialization, so it does match #2. — *end example*]
