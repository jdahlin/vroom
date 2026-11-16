# 7.5.7.2 Type requirements [expr.prim.req.type]

[Contents](README.md) · up: [7.5.7 Requires expressions \[expr.prim.req\]](expr.prim.req.md) · prev: [7.5.7.1 Simple requirements \[expr.prim.req.simple\]](expr.prim.req.simple.md) · next: [7.5.7.3 Compound requirements \[expr.prim.req.compound\]](expr.prim.req.compound.md)

```bnf
type-requirement:
    'typename' nested-name-specifierₒₚₜ type-name ';'
```

**1** A *type-requirement* asserts the validity of a type. [*Note 1*: The enclosing *requires-expression* will evaluate to `false` if substitution of template arguments fails. — *end note*] [*Example 1*:

```cpp
template<typename T, typename T::type = 0> struct S;
template<typename T> using Ref = T&;

template<typename T> concept C = requires {
  typename T::inner;    // required nested member name
  typename S<T>;        // required class template specialization
  typename Ref<T>;      // required alias template substitution, fails if T is void
};
```

— *end example*]

**2** A *type-requirement* that names a class template specialization does not require that type to be complete ([[basic.types]](basic.types.md)).
