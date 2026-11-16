# 13.5.3 Constraint normalization [temp.constr.normal]

[Contents](README.md) · up: [13.5 Template constraints \[temp.constr\]](temp.constr.md) · prev: [13.5.2 Constrained declarations \[temp.constr.decl\]](temp.constr.decl.md) · next: [13.5.4 Partial ordering by constraints \[temp.constr.order\]](temp.constr.order.md)

**1** The *normal form* of an *expression* `E` is a constraint ([[temp.constr.constr]](temp.constr.constr.md)) that is defined as follows:

- The normal form of an expression `( E )` is the normal form of `E`.

- The normal form of an expression `E1 || E2` is the disjunction ([[temp.constr.op]](temp.constr.op.md)) of the normal forms of `E1` and `E2`.

- The normal form of an expression `E1 && E2` is the conjunction of the normal forms of `E1` and `E2`.

- The normal form of a concept-id `C<A_1, A_2, ..., A_n>` is the normal form of the *constraint-expression* of `C`, after substituting `A_1, A_2, ..., A_n` for `C`’s respective template parameters in the parameter mappings in each atomic constraint. If any such substitution results in an invalid type or expression, the program is ill-formed; no diagnostic is required. [*Example 1*:

  ```cpp
  template<typename T> concept A = T::value || true;
  template<typename U> concept B = A<U*>;
  template<typename V> concept C = B<V&>;
  ```

  Normalization of `B`’s *constraint-expression* is valid and results in `T::value` (with the mapping `T` ↦ `U*`) ∨ `true` (with an empty mapping), despite the expression `T::value` being ill-formed for a pointer type `T`. Normalization of `C`’s *constraint-expression* results in the program being ill-formed, because it would form the invalid type `V&*` in the parameter mapping. — *end example*]

- The normal form of any other expression `E` is the atomic constraint whose expression is `E` and whose parameter mapping is the identity mapping.

**2** The process of obtaining the normal form of a *constraint-expression* is called *normalization*. [*Note 1*: Normalization of *constraint-expression*s is performed when determining the associated constraints ([[temp.constr.constr]](temp.constr.constr.md)) of a declaration and when evaluating the value of an *id-expression* that names a concept specialization ([[expr.prim.id]](expr.prim.id.md)). — *end note*]

**3** [*Example 2*:

```cpp
template<typename T> concept C1 = sizeof(T) == 1;
template<typename T> concept C2 = C1<T> && 1 == 2;
template<typename T> concept C3 = requires { typename T::type; };
template<typename T> concept C4 = requires (T x) { ++x; }

template<C2 U> void f1(U);      // #1
template<C3 U> void f2(U);      // #2
template<C4 U> void f3(U);      // #3
```

The associated constraints of #1 are `sizeof(T) == 1` (with mapping `T` ↦ `U`) ∧ `1 == 2`.<br> The associated constraints of #2 are `requires { typename T::type; }` (with mapping `T` ↦ `U`).<br> The associated constraints of #3 are `requires (T x) { ++x; }` (with mapping `T` ↦ `U`). — *end example*]
