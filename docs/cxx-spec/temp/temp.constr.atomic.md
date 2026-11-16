# 13.5.1.2 Atomic constraints [temp.constr.atomic]

[Contents](../README.md) · up: [13.5.1 Constraints \[temp.constr.constr\]](temp.constr.constr.md) · prev: [13.5.1.1 Logical operations \[temp.constr.op\]](temp.constr.op.md) · next: [13.5.2 Constrained declarations \[temp.constr.decl\]](temp.constr.decl.md)

**1** An *atomic constraint* is formed from an expression `E` and a mapping from the template parameters that appear within `E` to template arguments that are formed via substitution during constraint normalization in the declaration of a constrained entity (and, therefore, can involve the unsubstituted template parameters of the constrained entity), called the *parameter mapping* ([[temp.constr.decl]](temp.constr.decl.md)). [*Note 1*: Atomic constraints are formed by constraint normalization ([[temp.constr.normal]](temp.constr.normal.md)). `E` is never a logical AND expression ([[expr.log.and]](../expr/expr.log.and.md)) nor a logical OR expression ([[expr.log.or]](../expr/expr.log.or.md)). — *end note*]

**2** Two atomic constraints, *e*<sub>1</sub> and *e*<sub>2</sub>, are *identical* if they are formed from the same appearance of the same *expression* and if, given a hypothetical template *A* whose *template-parameter-list* consists of *template-parameter*s corresponding and equivalent ([[temp.over.link]](temp.over.link.md)) to those mapped by the parameter mappings of the expression, a *template-id* naming *A* whose *template-argument*s are the targets of the parameter mapping of *e*<sub>1</sub> is the same ([[temp.type]](temp.type.md)) as a *template-id* naming *A* whose *template-argument*s are the targets of the parameter mapping of *e*<sub>2</sub>. [*Note 2*: The comparison of parameter mappings of atomic constraints operates in a manner similar to that of declaration matching with alias template substitution ([[temp.alias]](temp.alias.md)). [*Example 1*:

```cpp
template <unsigned N> constexpr bool Atomic = true;
template <unsigned N> concept C = Atomic<N>;
template <unsigned N> concept Add1 = C<N + 1>;
template <unsigned N> concept AddOne = C<N + 1>;
template <unsigned M> void f()
  requires Add1<2 * M>;
template <unsigned M> int f()
  requires AddOne<2 * M> && true;

int x = f<0>();     // OK, the atomic constraints from concept C in both fs are Atomic<N>
                    // with mapping similar to N ↦ 2 * M + 1

template <unsigned N> struct WrapN;
template <unsigned N> using Add1Ty = WrapN<N + 1>;
template <unsigned N> using AddOneTy = WrapN<N + 1>;
template <unsigned M> void g(Add1Ty<2 * M> *);
template <unsigned M> void g(AddOneTy<2 * M> *);

void h() {
  g<0>(nullptr);    // OK, there is only one g
}
```

— *end example*] This similarity includes the situation where a program is ill-formed, no diagnostic required, when the meaning of the program depends on whether two constructs are equivalent, and they are functionally equivalent but not equivalent. [*Example 2*:

```cpp
template <unsigned N> void f2()
  requires Add1<2 * N>;
template <unsigned N> int f2()
  requires Add1<N * 2> && true;
void h2() {
  f2<0>();          // ill-formed, no diagnostic required:
                    // requires determination of subsumption between atomic constraints that are
                    // functionally equivalent but not equivalent
}
```

— *end example*] — *end note*]

**3** To determine if an atomic constraint is *satisfied*, the parameter mapping and template arguments are first substituted into its expression. If substitution results in an invalid type or expression, the constraint is not satisfied. Otherwise, the lvalue-to-rvalue conversion ([[conv.lval]](../expr/conv.lval.md)) is performed if necessary, and `E` shall be a constant expression of type `bool`. The constraint is satisfied if and only if evaluation of `E` results in `true`. If, at different points in the program, the satisfaction result is different for identical atomic constraints and template arguments, the program is ill-formed, no diagnostic required. [*Example 3*:

```cpp
template<typename T> concept C =
  sizeof(T) == 4 && !true;      // requires atomic constraints sizeof(T) == 4 and !true

template<typename T> struct S {
  constexpr operator bool() const { return true; }
};

template<typename T> requires (S<T>{})
void f(T);                      // #1
void f(int);                    // #2

void g() {
  f(0);                         // error: expression S<int>{} does not have type bool
}                               // while checking satisfaction of deduced arguments of #1;
                                // call is ill-formed even though #2 is a better match
```

— *end example*]
