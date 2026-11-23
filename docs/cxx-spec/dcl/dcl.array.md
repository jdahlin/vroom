# 9.3.3.4 Arrays [dcl.array]

[Contents](../README.md) · up: [9.3.3 Meaning of declarators \[dcl.meaning\]](dcl.meaning.md) · prev: [9.3.3.3 Pointers to members \[dcl.mptr\]](dcl.mptr.md) · next: [9.3.3.5 Functions \[dcl.fct\]](dcl.fct.md)

**1** In a declaration `T` `D` where `D` has the form

```bnf
'D1' '[' constant-expressionₒₚₜ ']' attribute-specifier-seqₒₚₜ
```

and the type of the contained *declarator-id* in the declaration `T` `D1` is “*derived-declarator-type-list* `T`”, the type of the *declarator-id* in `D` is “*derived-declarator-type-list* array of `N` `T`”. The *constant-expression* shall be a converted constant expression of type `std::size_t` ([[expr.const]](../expr/expr.const.md)). Its value `N` specifies the *array bound*, i.e., the number of elements in the array; `N` shall be greater than zero.

**2** In a declaration `T` `D` where `D` has the form

```bnf
'D1 [ ]' attribute-specifier-seqₒₚₜ
```

and the type of the contained *declarator-id* in the declaration `T` `D1` is “*derived-declarator-type-list* `T`”, the type of the *declarator-id* in `D` is “*derived-declarator-type-list* array of unknown bound of `T`”, except as specified below.

**3** A type of the form “array of `N` `U`” or “array of unknown bound of `U`” is an *array type*. The optional *attribute-specifier-seq* appertains to the array type.

**4** `U` is called the array *element type*; this type shall not be a placeholder type ([[dcl.spec.auto]](dcl.spec.auto.md)), a reference type, a function type, an array of unknown bound, or *cv* `void`. [*Note 1*: An array can be constructed from one of the fundamental types (except `void`), from a pointer, from a pointer to member, from a class, from an enumeration type, or from an array of known bound. — *end note*] [*Example 1*:

```cpp
float fa[17], *afp[17];
```

declares an array of `float` numbers and an array of pointers to `float` numbers. — *end example*]

**5** Any type of the form “*cv-qualifier-seq* array of `N` `U`” is adjusted to “array of `N` *cv-qualifier-seq* `U`”, and similarly for “array of unknown bound of `U`”. [*Example 2*:

```cpp
typedef int A[5], AA[2][3];
typedef const A CA;             // type is ``array of 5 const int''
typedef const AA CAA;           // type is ``array of 2 array of 3 const int''
```

— *end example*] [*Note 2*: An “array of `N` *cv-qualifier-seq* `U`” has cv-qualified type; see [[basic.type.qualifier]](../basic/basic.type.qualifier.md). — *end note*]

**6** An object of type “array of `N` `U`” contains a contiguously allocated non-empty set of `N` subobjects of type `U`, known as the *elements* of the array, and numbered `0` to `N-1`.

**7** In addition to declarations in which an incomplete object type is allowed, an array bound may be omitted in some cases in the declaration of a function parameter ([[dcl.fct]](dcl.fct.md)). An array bound may also be omitted when an object (but not a non-static data member) of array type is initialized and the declarator is followed by an initializer ([[dcl.init]](dcl.init.md), [[class.mem]](../class/class.mem.md), [[expr.type.conv]](../expr/expr.type.conv.md), [[expr.new]](../expr/expr.new.md)). In these cases, the array bound is calculated from the number of initial elements (say, `N`) supplied ([[dcl.init.aggr]](dcl.init.aggr.md)), and the type of the array is “array of `N` `U`”.

**8** Furthermore, if there is a preceding declaration of the entity in the same scope in which the bound was specified, an omitted array bound is taken to be the same as in that earlier declaration, and similarly for the definition of a static data member of a class. [*Example 3*:

```cpp
extern int x[10];
struct S {
  static int y[10];
};

int x[];                // OK: bound is 10
int S::y[];             // OK: bound is 10

void f() {
  extern int x[];
  int i = sizeof(x);    // error: incomplete object type
}
```

— *end example*]

**9** [*Note 3*: When several “array of” specifications are adjacent, a multidimensional array type is created; only the first of the constant expressions that specify the bounds of the arrays may be omitted. [*Example 4*:

```cpp
int x3d[3][5][7];
```

declares an array of three elements, each of which is an array of five elements, each of which is an array of seven integers. The overall array can be viewed as a three-dimensional array of integers, with rank 3 × 5 × 7. Any of the expressions `x3d`, `x3d[i]`, `x3d[i][j]`, `x3d[i][j][k]` can reasonably appear in an expression. The expression `x3d[i]` is equivalent to `*(x3d + i)`; in that expression, `x3d` is subject to the array-to-pointer conversion ([[conv.array]](../expr/conv.array.md)) and is first converted to a pointer to a 2-dimensional array with rank 5 × 7 that points to the first element of `x3d`. Then `i` is added, which on typical implementations involves multiplying `i` by the length of the object to which the pointer points, which is `sizeof(int)`× 5 × 7. The result of the addition and indirection is an lvalue denoting the `i`<sup></sup>*th* array element of `x3d` (an array of five arrays of seven integers). If there is another subscript, the same argument applies again, so `x3d[i][j]` is an lvalue denoting the `j`<sup></sup>*th* array element of the `i`<sup></sup>*th* array element of `x3d` (an array of seven integers), and `x3d[i][j][k]` is an lvalue denoting the `k`<sup></sup>*th* array element of the `j`<sup></sup>*th* array element of the `i`<sup></sup>*th* array element of `x3d` (an integer). — *end example*] The first subscript in the declaration helps determine the amount of storage consumed by an array but plays no other part in subscript calculations. — *end note*]

**10** [*Note 4*: Conversions affecting expressions of array type are described in [[conv.array]](../expr/conv.array.md). — *end note*]

**11** [*Note 5*: The subscript operator can be overloaded for a class ([[over.sub]](../over/over.sub.md)). For the operator’s built-in meaning, see [[expr.sub]](../expr/expr.sub.md). — *end note*]
