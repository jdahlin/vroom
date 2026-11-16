# 7.5.7.3 Compound requirements [expr.prim.req.compound]

[Contents](../README.md) · up: [7.5.7 Requires expressions \[expr.prim.req\]](expr.prim.req.md) · prev: [7.5.7.2 Type requirements \[expr.prim.req.type\]](expr.prim.req.type.md) · next: [7.5.7.4 Nested requirements \[expr.prim.req.nested\]](expr.prim.req.nested.md)

```bnf
compound-requirement:
    '{' expression '}' 'noexcept'ₒₚₜ return-type-requirementₒₚₜ ';'
```

```bnf
return-type-requirement:
    '->' type-constraint
```

**1** A *compound-requirement* asserts properties of the *expression* *E*. Substitution of template arguments (if any) and verification of semantic properties proceed in the following order:

- Substitution of template arguments (if any) into the *expression* is performed.

- If the `noexcept` specifier is present, *E* shall not be a potentially-throwing expression ([[except.spec]](../except/except.spec.md)).

- If the *return-type-requirement* is present, then:

  - Substitution of template arguments (if any) into the *return-type-requirement* is performed.

  - The immediately-declared constraint ([[temp.param]](../temp/temp.param.md)) of the *type-constraint* for `decltype((E))` shall be satisfied. [*Example 1*: Given concepts `C` and `D`,

    ```cpp
    requires {
      { E1 } -> C;
      { E2 } -> D<A_1, ..., A_n>;
    };
    ```

    is equivalent to

    ```cpp
    requires {
      E1; requires C<decltype((E1))>;
      E2; requires D<decltype((E2)), A_1, ..., A_n>;
    };
    ```

    (including in the case where *n* is zero). — *end example*]

**2** [*Example 2*:

```cpp
template<typename T> concept C1 = requires(T x) {
  {x++};
};
```

The *compound-requirement* in `C1` requires that `x++` is a valid expression. It is equivalent to the *simple-requirement* `x++;`.

```cpp
template<typename T> concept C2 = requires(T x) {
  {*x} -> std::same_as<typename T::inner>;
};
```

The *compound-requirement* in `C2` requires that `*x` is a valid expression, that `typename T::inner` is a valid type, and that `std::same_as<decltype((*x)), typename T::inner>` is satisfied.

```cpp
template<typename T> concept C3 =
  requires(T x) {
    {g(x)} noexcept;
  };
```

The *compound-requirement* in `C3` requires that `g(x)` is a valid expression and that `g(x)` is non-throwing. — *end example*]
