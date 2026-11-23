# 13.7.1.2 Deduction guides [temp.deduct.guide]

[Contents](../README.md) · up: [13.7.1 Class templates \[temp.class\]](temp.class.md) · prev: [13.7.1.1 Member functions of class templates \[temp.mem.func\]](temp.mem.func.md) · next: [13.7.1.3 Member classes of class templates \[temp.mem.class\]](temp.mem.class.md)

**1** Deduction guides are used when a *template-name* appears as a type specifier for a deduced class type ([[dcl.type.class.deduct]](../dcl/dcl.type.class.deduct.md)). Deduction guides are not found by name lookup. Instead, when performing class template argument deduction ([[over.match.class.deduct]](../over/over.match.class.deduct.md)), any deduction guides declared for the class template are considered.

```bnf
deduction-guide:
    explicit-specifierₒₚₜ template-name '(' parameter-declaration-clause ')' '->' simple-template-id ';'
```

**2** [*Example 1*:

```cpp
template<class T, class D = int>
struct S {
  T data;
};
template<class U>
S(U) -> S<typename U::type>;

struct A {
  using type = short;
  operator type();
};
S x{A()};           // x is of type S<short, int>
```

— *end example*]

**3** The same restrictions apply to the *parameter-declaration-clause* of a deduction guide as in a function declaration ([[dcl.fct]](../dcl/dcl.fct.md)). The *simple-template-id* shall name a class template specialization. The *template-name* shall be the same *identifier* as the *template-name* of the *simple-template-id*. A *deduction-guide* shall be declared in the same scope as the corresponding class template and, for a member class template, with the same access. Two deduction guide declarations in the same translation unit for the same class template shall not have equivalent *parameter-declaration-clause*s.
