# 7.6.1.10 Const cast [expr.const.cast]

[Contents](README.md) · up: [7.6.1 Postfix expressions \[expr.post\]](expr.post.md) · prev: [7.6.1.9 Reinterpret cast \[expr.reinterpret.cast\]](expr.reinterpret.cast.md) · next: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md)

**1** The result of the expression `const_cast<T>(v)` is of type `T`. If `T` is an lvalue reference to object type, the result is an lvalue; if `T` is an rvalue reference to object type, the result is an xvalue; otherwise, the result is a prvalue and the lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions are performed on the expression `v`. Conversions that can be performed explicitly using `const_cast` are listed below. No other conversion shall be performed explicitly using `const_cast`.

**2** [*Note 1*: Subject to the restrictions in this subclause, an expression may be cast to its own type using a `const_cast` operator. — *end note*]

**3** For two similar types `T1` and `T2` ([[conv.qual]](conv.qual.md)), a prvalue of type `T1` may be explicitly converted to the type `T2` using a `const_cast` if, considering the cv-decompositions of both types, each *P*<sup>1</sup><sub>*i*</sub> is the same as *P*<sup>2</sup><sub>*i*</sub> for all *i*. The result of a `const_cast` refers to the original entity. [*Example 1*:

```cpp
typedef int *A[3];                  // array of 3 pointer to int
typedef const int *const CA[3];     // array of 3 const pointer to const int

CA &&r = A{};                       // OK, reference binds to temporary array object
                                    // after qualification conversion to type CA
A &&r1 = const_cast<A>(CA{});       // error: temporary array decayed to pointer
A &&r2 = const_cast<A&&>(CA{});     // OK
```

— *end example*]

**4** For two object types `T1` and `T2`, if a pointer to `T1` can be explicitly converted to the type “pointer to `T2`” using a `const_cast`, then the following conversions can also be made:

- an lvalue of type `T1` can be explicitly converted to an lvalue of type `T2` using the cast `const_cast<T2&>`;
- a glvalue of type `T1` can be explicitly converted to an xvalue of type `T2` using the cast `const_cast<T2&&>`; and
- if `T1` is a class type, a prvalue of type `T1` can be explicitly converted to an xvalue of type `T2` using the cast `const_cast<T2&&>`.

The result of a reference `const_cast` refers to the original object if the operand is a glvalue and to the result of applying the temporary materialization conversion ([[conv.rval]](conv.rval.md)) otherwise.

**5** A null pointer value ([[basic.compound]](basic.compound.md)) is converted to the null pointer value of the destination type. The null member pointer value ([[conv.mem]](conv.mem.md)) is converted to the null member pointer value of the destination type.

**6** [*Note 2*: Depending on the type of the object, a write operation through the pointer, lvalue or pointer to data member resulting from a `const_cast` that casts away a const-qualifier[^1] may produce undefined behavior ([[dcl.type.cv]](dcl.type.cv.md)). — *end note*]

**7** A conversion from a type `T1` to a type `T2` *casts away constness* if `T1` and `T2` are different, there is a cv-decomposition ([[conv.qual]](conv.qual.md)) of `T1` yielding *n* such that `T2` has a cv-decomposition of the form *cv*<sub>0</sub><sup>2</sup> *P*<sub>0</sub><sup>2</sup> *cv*<sub>1</sub><sup>2</sup> *P*<sub>1</sub><sup>2</sup> … *cv*<sub>*n*-1</sub><sup>2</sup> *P*<sub>*n*-1</sub><sup>2</sup> *cv*<sub>*n*</sub><sup>2</sup> `U`<sub>2</sub>, and there is no qualification conversion that converts `T1` to *cv*<sub>0</sub><sup>2</sup> *P*<sub>0</sub><sup>1</sup> *cv*<sub>1</sub><sup>2</sup> *P*<sub>1</sub><sup>1</sup> … *cv*<sub>*n*-1</sub><sup>2</sup> *P*<sub>*n*-1</sub><sup>1</sup> *cv*<sub>*n*</sub><sup>2</sup> `U`<sub>1</sub>.

**8** Casting from an lvalue of type `T1` to an lvalue of type `T2` using an lvalue reference cast or casting from an expression of type `T1` to an xvalue of type `T2` using an rvalue reference cast casts away constness if a cast from a prvalue of type “pointer to `T1`” to the type “pointer to `T2`” casts away constness.

**9** [*Note 3*: Some conversions which involve only changes in cv-qualification cannot be done using `const_cast.` For instance, conversions between pointers to functions are not covered because such conversions lead to values whose use causes undefined behavior. For the same reasons, conversions between pointers to member functions, and in particular, the conversion from a pointer to a const member function to a pointer to a non-const member function, are not covered. — *end note*]

[^1]: `const_cast` is not limited to conversions that cast away a const-qualifier.
