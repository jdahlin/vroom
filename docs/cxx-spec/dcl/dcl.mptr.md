# 9.3.3.3 Pointers to members [dcl.mptr]

[Contents](../README.md) · up: [9.3.3 Meaning of declarators \[dcl.meaning\]](dcl.meaning.md) · prev: [9.3.3.2 References \[dcl.ref\]](dcl.ref.md) · next: [9.3.3.4 Arrays \[dcl.array\]](dcl.array.md)

**1** In a declaration `T` `D` where `D` has the form

```bnf
nested-name-specifier '*' attribute-specifier-seqₒₚₜ cv-qualifier-seqₒₚₜ 'D1'
```

and the *nested-name-specifier* denotes a class, and the type of the identifier in the declaration `T` `D1` is “*derived-declarator-type-list* `T`”, then the type of the identifier of `D` is “*derived-declarator-type-list* *cv-qualifier-seq* pointer to member of class *nested-name-specifier* of type `T`”. The optional *attribute-specifier-seq* ([[dcl.attr.grammar]](dcl.attr.grammar.md)) appertains to the pointer-to-member.

**2** [*Example 1*:

```cpp
struct X {
  void f(int);
  int a;
};
struct Y;

int X::* pmi = &X::a;
void (X::* pmf)(int) = &X::f;
double X::* pmd;
char Y::* pmc;
```

declares `pmi`, `pmf`, `pmd` and `pmc` to be a pointer to a member of `X` of type `int`, a pointer to a member of `X` of type `void(int)`, a pointer to a member of `X` of type `double` and a pointer to a member of `Y` of type `char` respectively. The declaration of `pmd` is well-formed even though `X` has no members of type `double`. Similarly, the declaration of `pmc` is well-formed even though `Y` is an incomplete type. `pmi` and `pmf` can be used like this:

```cpp
X obj;
// ...
obj.*pmi = 7;       // assign 7 to an integer member of obj
(obj.*pmf)(7);      // call a function member of obj with the argument 7
```

— *end example*]

**3** A pointer to member shall not point to a static member of a class ([[class.static]](../class/class.static.md)), a member with reference type, or “*cv* `void`”.

**4** [*Note 1*: See also [[expr.unary]](../expr/expr.unary.md) and [[expr.mptr.oper]](../expr/expr.mptr.oper.md). The type “pointer to member” is distinct from the type “pointer”, that is, a pointer to member is declared only by the pointer-to-member declarator syntax, and never by the pointer declarator syntax. There is no “reference-to-member” type in C++. — *end note*]
