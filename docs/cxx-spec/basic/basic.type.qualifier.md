# 6.8.3 CV-qualifiers [basic.type.qualifier]

[Contents](../README.md) · up: [6.8 Types \[basic.types\]](basic.types.md) · prev: [6.8.2 Compound types \[basic.compound\]](basic.compound.md) · next: [6.8.4 Integer conversion rank \[conv.rank\]](conv.rank.md)

**1** A type mentioned in [[basic.fundamental]](basic.fundamental.md) and [[basic.compound]](basic.compound.md) is a *cv-unqualified type*. Each type which is a cv-unqualified complete or incomplete object type or is `void` ([[basic.types]](basic.types.md)) has three corresponding cv-qualified versions of its type: a *const-qualified* version, a *volatile-qualified* version, and a *const-volatile-qualified* version. The type of an object ([[intro.object]](intro.object.md)) includes the *cv-qualifier*s specified in the *decl-specifier-seq* ([[dcl.spec]](../dcl/dcl.spec.md)), *declarator* ([[dcl.decl]](../dcl/dcl.decl.md)), *type-id* ([[dcl.name]](../dcl/dcl.name.md)), or *new-type-id* ([[expr.new]](../expr/expr.new.md)) when the object is created.

- A *const object* is an object of type `const T` or a non-mutable subobject of a const object.
- A *volatile object* is an object of type `volatile T` or a subobject of a volatile object.
- A *const volatile object* is an object of type `const volatile T`, a non-mutable subobject of a const volatile object, a const subobject of a volatile object, or a non-mutable volatile subobject of a const object.

The cv-qualified or cv-unqualified versions of a type are distinct types; however, they shall have the same representation and alignment requirements ([[basic.align]](basic.align.md)).[^1]

**2** Except for array types, a compound type ([[basic.compound]](basic.compound.md)) is not cv-qualified by the cv-qualifiers (if any) of the types from which it is compounded.

**3** An array type whose elements are cv-qualified is also considered to have the same cv-qualifications as its elements. [*Note 1*: Cv-qualifiers applied to an array type attach to the underlying element type, so the notation “*cv* `T`”, where `T` is an array type, refers to an array whose elements are so-qualified ([[dcl.array]](../dcl/dcl.array.md)). — *end note*] [*Example 1*:

```cpp
typedef char CA[5];
typedef const char CC;
CC arr1[5] = { 0 };
const CA arr2 = { 0 };
```

The type of both `arr1` and `arr2` is “array of 5 `const char`”, and the array type is considered to be const-qualified. — *end example*]

**4** [*Note 2*: See [[dcl.fct]](../dcl/dcl.fct.md) and [[class.this]](../class/class.this.md) regarding function types that have *cv-qualifier*s. — *end note*]

**5** There is a partial ordering on cv-qualifiers, so that a type can be said to be *more cv-qualified* than another. [Table 13](basic.type.qualifier.md#tab:basic.type.qualifier.rel) shows the relations that constitute this ordering.

<a id="tab:basic.type.qualifier.rel"></a>
**Table 13: Relations on `const` and `volatile`** [tab:basic.type.qualifier.rel]

|  |  |  |
| --- | --- | --- |
| *no cv-qualifier* | &lt; | `const` |
| *no cv-qualifier* | &lt; | `volatile` |
| *no cv-qualifier* | &lt; | `const volatile` |
| `const` | &lt; | `const volatile` |
| `volatile` | &lt; | `const volatile` |

**6** In this document, the notation *cv* (or *cv1*, *cv2*, etc.), used in the description of types, represents an arbitrary set of cv-qualifiers, i.e., one of {`const`}, {`volatile`}, {`const`, `volatile`}, or the empty set. For a type *cv* `T`, the *top-level cv-qualifiers* of that type are those denoted by *cv*. [*Example 2*: The type corresponding to the *type-id* `const int&` has no top-level cv-qualifiers. The type corresponding to the *type-id* `volatile int * const` has the top-level cv-qualifier `const`. For a class type `C`, the type corresponding to the *type-id* `void (C::* volatile)(int) const` has the top-level cv-qualifier `volatile`. — *end example*]

[^1]: The same representation and alignment requirements are meant to imply interchangeability as arguments to functions, return values from functions, and non-static data members of unions.
