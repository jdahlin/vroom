# 7.6.2.8 Delete [expr.delete]

[Contents](../README.md) · up: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md) · prev: [7.6.2.7 New \[expr.new\]](expr.new.md) · next: [7.6.3 Explicit type conversion (cast notation) \[expr.cast\]](expr.cast.md)

**1** The *delete-expression* operator destroys a most derived object ([[intro.object]](../basic/intro.object.md)) or array created by a *new-expression*.

```bnf
delete-expression:
    '::'ₒₚₜ 'delete' cast-expression
    '::'ₒₚₜ 'delete' '[' ']' cast-expression
```

The first alternative is a *single-object delete expression*, and the second is an *array delete expression*. Whenever the `delete` keyword is immediately followed by empty square brackets, it shall be interpreted as the second alternative.[^1] The operand shall be of pointer to object type or of class type. If of class type, the operand is contextually implicitly converted ([[conv]](conv.md)) to a pointer to object type.[^2] The *delete-expression*’s result has type `void`.

**2** If the operand has a class type, the operand is converted to a pointer type by calling the above-mentioned conversion function, and the converted operand is used in place of the original operand for the remainder of this subclause. In a single-object delete expression, the value of the operand of `delete` may be a null pointer value, a pointer to a non-array object created by a previous *new-expression*, or a pointer to a subobject ([[intro.object]](../basic/intro.object.md)) representing a base class of such an object ([[class.derived]](../class/class.derived.md)). If not, the behavior is undefined. In an array delete expression, the value of the operand of `delete` may be a null pointer value or a pointer value that resulted from a previous array *new-expression*.[^3] If not, the behavior is undefined. [*Note 1*: This means that the syntax of the *delete-expression* must match the type of the object allocated by `new`, not the syntax of the *new-expression*. — *end note*] [*Note 2*: A pointer to a `const` type can be the operand of a *delete-expression*; it is not necessary to cast away the constness ([[expr.const.cast]](expr.const.cast.md)) of the pointer expression before it is used as the operand of the *delete-expression*. — *end note*]

**3** In a single-object delete expression, if the static type of the object to be deleted is different from its dynamic type and the selected deallocation function (see below) is not a destroying operator delete, the static type shall be a base class of the dynamic type of the object to be deleted and the static type shall have a virtual destructor or the behavior is undefined. In an array delete expression, if the dynamic type of the object to be deleted differs from its static type, the behavior is undefined.

**4** The *cast-expression* in a *delete-expression* shall be evaluated exactly once.

**5** If the object being deleted has incomplete class type at the point of deletion and the complete class has a non-trivial destructor or a deallocation function, the behavior is undefined.

**6** If the value of the operand of the *delete-expression* is not a null pointer value and the selected deallocation function (see below) is not a destroying operator delete, the *delete-expression* will invoke the destructor (if any) for the object or the elements of the array being deleted. In the case of an array, the elements will be destroyed in order of decreasing address (that is, in reverse order of the completion of their constructor; see [[class.base.init]](../class/class.base.init.md)).

**7** If the value of the operand of the *delete-expression* is not a null pointer value, then:

- If the allocation call for the *new-expression* for the object to be deleted was not omitted and the allocation was not extended ([[expr.new]](expr.new.md)), the *delete-expression* shall call a deallocation function ([[basic.stc.dynamic.deallocation]](../basic/basic.stc.dynamic.deallocation.md)). The value returned from the allocation call of the *new-expression* shall be passed as the first argument to the deallocation function.
- Otherwise, if the allocation was extended or was provided by extending the allocation of another *new-expression*, and the *delete-expression* for every other pointer value produced by a *new-expression* that had storage provided by the extended *new-expression* has been evaluated, the *delete-expression* shall call a deallocation function. The value returned from the allocation call of the extended *new-expression* shall be passed as the first argument to the deallocation function.
- Otherwise, the *delete-expression* will not call a deallocation function.

[*Note 3*: The deallocation function is called regardless of whether the destructor for the object or some element of the array throws an exception. — *end note*] If the value of the operand of the *delete-expression* is a null pointer value, it is unspecified whether a deallocation function will be called as described above.

**8** [*Note 4*: An implementation provides default definitions of the global deallocation functions `operator delete` for non-arrays ([[new.delete.single]](../lib/support/new.delete.single.md)) and `operator delete[]` for arrays ([[new.delete.array]](../lib/support/new.delete.array.md)). A C++ program can provide alternative definitions of these functions ([[replacement.functions]](../lib/library/replacement.functions.md)), and/or class-specific versions ([[class.free]](../class/class.free.md)). — *end note*]

**9** When the keyword `delete` in a *delete-expression* is preceded by the unary `::` operator, the deallocation function’s name is looked up in global scope. Otherwise, the lookup considers class-specific deallocation functions ([[class.free]](../class/class.free.md)). If no class-specific deallocation function is found, the deallocation function’s name is looked up in global scope.

**10** If deallocation function lookup finds more than one usual deallocation function, the function to be called is selected as follows:

- If any of the deallocation functions is a destroying operator delete, all deallocation functions that are not destroying operator deletes are eliminated from further consideration.
- If the type has new-extended alignment, a function with a parameter of type `std::align_val_t` is preferred; otherwise a function without such a parameter is preferred. If any preferred functions are found, all non-preferred functions are eliminated from further consideration.
- If exactly one function remains, that function is selected and the selection process terminates.
- If the deallocation functions have class scope, the one without a parameter of type `std::size_t` is selected.
- If the type is complete and if, for an array delete expression only, the operand is a pointer to a class type with a non-trivial destructor or a (possibly multi-dimensional) array thereof, the function with a parameter of type `std::size_t` is selected.
- Otherwise, it is unspecified whether a deallocation function with a parameter of type `std::size_t` is selected.

**11** For a single-object delete expression, the deleted object is the object denoted by the operand if its static type does not have a virtual destructor, and its most-derived object otherwise. [*Note 5*: If the deallocation function is not a destroying operator delete and the deleted object is not the most derived object in the former case, the behavior is undefined, as stated above. — *end note*] For an array delete expression, the deleted object is the array object. When a *delete-expression* is executed, the selected deallocation function shall be called with the address of the deleted object in a single-object delete expression, or the address of the deleted object suitably adjusted for the array allocation overhead ([[expr.new]](expr.new.md)) in an array delete expression, as its first argument. [*Note 6*: Any cv-qualifiers in the type of the deleted object are ignored when forming this argument. — *end note*] If a destroying operator delete is used, an unspecified value is passed as the argument corresponding to the parameter of type `std::destroying_delete_t`. If a deallocation function with a parameter of type `std::align_val_t` is used, the alignment of the type of the deleted object is passed as the corresponding argument. If a deallocation function with a parameter of type `std::size_t` is used, the size of the deleted object in a single-object delete expression, or of the array plus allocation overhead in an array delete expression, is passed as the corresponding argument. [*Note 7*: If this results in a call to a replaceable deallocation function, and either the first argument was not the result of a prior call to a replaceable allocation function or the second or third argument was not the corresponding argument in said call, the behavior is undefined ([[new.delete.single]](../lib/support/new.delete.single.md), [[new.delete.array]](../lib/support/new.delete.array.md)). — *end note*]

**12** Access and ambiguity control are done for both the deallocation function and the destructor ([[class.dtor]](../class/class.dtor.md), [[class.free]](../class/class.free.md)).

[^1]: A *lambda-expression* with a *lambda-introducer* that consists of empty square brackets can follow the `delete` keyword if the *lambda-expression* is enclosed in parentheses.
[^2]: This implies that an object cannot be deleted using a pointer of type `void*` because `void` is not an object type.
[^3]: For nonzero-length arrays, this is the same as a pointer to the first element of the array created by that *new-expression*. Zero-length arrays do not have a first element.
