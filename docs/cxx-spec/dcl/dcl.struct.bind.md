# 9.6 Structured binding declarations [dcl.struct.bind]

[Contents](../README.md) · up: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · prev: [9.5.4 Coroutine definitions \[dcl.fct.def.coroutine\]](dcl.fct.def.coroutine.md) · next: [9.7 Enumerations \[enum\]](enum.md)

**1** A structured binding declaration introduces the *identifier*s `v`<sub>0</sub>, `v`<sub>1</sub>, `v`<sub>2</sub>, … of the *identifier-list* as names ([[basic.scope.declarative]](../basic/basic.scope.declarative.md)) of *structured binding*s. Let *cv* denote the *cv-qualifier*s in the *decl-specifier-seq* and *S* consist of the *storage-class-specifier*s of the *decl-specifier-seq* (if any). A *cv* that includes `volatile` is deprecated; see [[depr.volatile.type]](../depr/depr.volatile.type.md). First, a variable with a unique name `e` is introduced. If the *assignment-expression* in the *initializer* has array type `A` and no *ref-qualifier* is present, `e` is defined by

```bnf
attribute-specifier-seqₒₚₜ S cv 'A' e ';'
```

and each element is copy-initialized or direct-initialized from the corresponding element of the *assignment-expression* as specified by the form of the *initializer*. Otherwise, `e` is defined as-if by

```bnf
attribute-specifier-seqₒₚₜ decl-specifier-seq ref-qualifierₒₚₜ e initializer ';'
```

where the declaration is never interpreted as a function declaration and the parts of the declaration other than the *declarator-id* are taken from the corresponding structured binding declaration. The type of the *id-expression* `e` is called `E`. [*Note 1*: `E` is never a reference type ([[expr.prop]](../expr/expr.prop.md)). — *end note*]

**2** If the *initializer* refers to one of the names introduced by the structured binding declaration, the program is ill-formed.

**3** If `E` is an array type with element type `T`, the number of elements in the *identifier-list* shall be equal to the number of elements of `E`. Each `v`<sub>*i*</sub> is the name of an lvalue that refers to the element *i* of the array and whose type is `T`; the referenced type is `T`. [*Note 2*: The top-level cv-qualifiers of `T` are *cv*. — *end note*] [*Example 1*:

```cpp
auto f() -> int(&)[2];
auto [ x, y ] = f();            // x and y refer to elements in a copy of the array return value
auto& [ xr, yr ] = f();         // xr and yr refer to elements in the array referred to by f's return value
```

— *end example*]

**4** Otherwise, if the *qualified-id* `std::tuple_size<E>` names a complete class type with a member named `value`, the expression `std::tuple_size<E>::value` shall be a well-formed integral constant expression and the number of elements in the *identifier-list* shall be equal to the value of that expression. Let `i` be an index prvalue of type `std::size_t` corresponding to `v`<sub></sub>*i*. The *unqualified-id* `get` is looked up in the scope of `E` by class member access lookup ([[basic.lookup.classref]](../basic/basic.lookup.classref.md)), and if that finds at least one declaration that is a function template whose first template parameter is a non-type parameter, the initializer is `e.get<i>()`. Otherwise, the initializer is `get<i>(e)`, where `get` is looked up in the associated namespaces ([[basic.lookup.argdep]](../basic/basic.lookup.argdep.md)). In either case, `get<i>` is interpreted as a *template-id*. [*Note 3*: Ordinary unqualified lookup ([[basic.lookup.unqual]](../basic/basic.lookup.unqual.md)) is not performed. — *end note*] In either case, `e` is an lvalue if the type of the entity `e` is an lvalue reference and an xvalue otherwise. Given the type `T`<sub>*i*</sub> designated by `std::tuple_element<i, E>::type` and the type `U`<sub>*i*</sub> designated by either `T_i&` or `T_i&&`, where `U`<sub>*i*</sub> is an lvalue reference if the initializer is an lvalue and an rvalue reference otherwise, variables are introduced with unique names `r`<sub>*i*</sub> as follows:

```bnf
S 'U_i r_i =' initializer ';'
```

Each `v`<sub>*i*</sub> is the name of an lvalue of type `T`<sub>*i*</sub> that refers to the object bound to `r`<sub>*i*</sub>; the referenced type is `T`<sub>*i*</sub>.

**5** Otherwise, all of `E`’s non-static data members shall be direct members of `E` or of the same base class of `E`, well-formed when named as `e.name` in the context of the structured binding, `E` shall not have an anonymous union member, and the number of elements in the *identifier-list* shall be equal to the number of non-static data members of `E`. Designating the non-static data members of `E` as `m`<sub>0</sub>, `m`<sub>1</sub>, `m`<sub>2</sub>, … (in declaration order), each `v`<sub>*i*</sub> is the name of an lvalue that refers to the member `m`<sub>*i*</sub> of `e` and whose type is *cv* `T`<sub>*i*</sub>, where `T`<sub>*i*</sub> is the declared type of that member; the referenced type is *cv* `T`<sub>*i*</sub>. The lvalue is a bit-field if that member is a bit-field. [*Example 2*:

```cpp
struct S { int x1 : 2; volatile double y1; };
S f();
const auto [ x, y ] = f();
```

The type of the *id-expression* `x` is “`const int`”, the type of the *id-expression* `y` is “`const volatile double`”. — *end example*]
