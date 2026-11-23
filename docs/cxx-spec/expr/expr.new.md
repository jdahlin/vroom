# 7.6.2.7 New [expr.new]

[Contents](../README.md) · up: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md) · prev: [7.6.2.6 `noexcept` operator \[expr.unary.noexcept\]](expr.unary.noexcept.md) · next: [7.6.2.8 Delete \[expr.delete\]](expr.delete.md)

**1** The *new-expression* attempts to create an object of the *type-id* ([[dcl.name]](../dcl/dcl.name.md)) or *new-type-id* to which it is applied. The type of that object is the *allocated type*. This type shall be a complete object type, but not an abstract class type or array thereof ([[intro.object]](../basic/intro.object.md), [[basic.types]](../basic/basic.types.md), [[class.abstract]](../class/class.abstract.md)). [*Note 1*: Because references are not objects, references cannot be created by *new-expression*s. — *end note*] [*Note 2*: The *type-id* may be a cv-qualified type, in which case the object created by the *new-expression* has a cv-qualified type. — *end note*]

```bnf
new-expression:
    '::'ₒₚₜ 'new' new-placementₒₚₜ new-type-id new-initializerₒₚₜ
    '::'ₒₚₜ 'new' new-placementₒₚₜ '(' type-id ')' new-initializerₒₚₜ
```

```bnf
new-placement:
    '(' expression-list ')'
```

```bnf
new-type-id:
    type-specifier-seq new-declaratorₒₚₜ
```

```bnf
new-declarator:
    ptr-operator new-declaratorₒₚₜ
    noptr-new-declarator
```

```bnf
noptr-new-declarator:
    '[' expressionₒₚₜ ']' attribute-specifier-seqₒₚₜ
    noptr-new-declarator '[' constant-expression ']' attribute-specifier-seqₒₚₜ
```

```bnf
new-initializer:
    '(' expression-listₒₚₜ ')'
    braced-init-list
```

**2** If a placeholder type ([[dcl.spec.auto]](../dcl/dcl.spec.auto.md)) appears in the *type-specifier-seq* of a *new-type-id* or *type-id* of a *new-expression*, the allocated type is deduced as follows: Let *init* be the *new-initializer*, if any, and `T` be the *new-type-id* or *type-id* of the *new-expression*, then the allocated type is the type deduced for the variable `x` in the invented declaration ([[dcl.spec.auto]](../dcl/dcl.spec.auto.md)):

```cpp
T x init ;
```

[*Example 1*:

```cpp
new auto(1);                    // allocated type is int
auto x = new auto('a');         // allocated type is char, x is of type char*

template<class T> struct A { A(T, T); };
auto y = new A{1, 2};           // allocated type is A<int>
```

— *end example*]

**3** The *new-type-id* in a *new-expression* is the longest possible sequence of *new-declarator*s. [*Note 3*: This prevents ambiguities between the declarator operators `&`, `&&`, `*`, and `[]` and their expression counterparts. — *end note*] [*Example 2*:

```cpp
new int * i;                    // syntax error: parsed as (new int*) i, not as (new int)*i
```

The `*` is the pointer declarator and not the multiplication operator. — *end example*]

**4** [*Note 4*: Parentheses in a *new-type-id* of a *new-expression* can have surprising effects. [*Example 3*:

```cpp
new int(*[10])();               // error
```

is ill-formed because the binding is

```cpp
(new int) (*[10])();            // error
```

Instead, the explicitly parenthesized version of the `new` operator can be used to create objects of compound types ([[basic.compound]](../basic/basic.compound.md)):

```cpp
new (int (*[10])());
```

allocates an array of `10` pointers to functions (taking no argument and returning `int`). — *end example*] — *end note*]

**5** Objects created by a *new-expression* have dynamic storage duration ([[basic.stc.dynamic]](../basic/basic.stc.dynamic.md)). [*Note 5*: The lifetime of such an object is not necessarily restricted to the scope in which it is created. — *end note*] When the allocated object is not an array, the result of the *new-expression* is a pointer to the object created.

**6** When the allocated object is an array (that is, the *noptr-new-declarator* syntax is used or the *new-type-id* or *type-id* denotes an array type), the *new-expression* yields a pointer to the initial element (if any) of the array. [*Note 6*: Both `new int` and `new int[10]` have type `int*` and the type of `new int[i][10]` is `int (*)[10]` — *end note*] The *attribute-specifier-seq* in a *noptr-new-declarator* appertains to the associated array type.

**7** Every *constant-expression* in a *noptr-new-declarator* shall be a converted constant expression ([[expr.const]](expr.const.md)) of type `std::size_t` and its value shall be greater than zero. [*Example 4*: Given the definition `int n = 42`, `new float[n][5]` is well-formed (because `n` is the *expression* of a *noptr-new-declarator*), but `new float[5][n]` is ill-formed (because `n` is not a constant expression). — *end example*]

**8** If the *type-id* or *new-type-id* denotes an array type of unknown bound ([[dcl.array]](../dcl/dcl.array.md)), the *new-initializer* shall not be omitted; the allocated object is an array with `n` elements, where `n` is determined from the number of initial elements supplied in the *new-initializer* ([[dcl.init.aggr]](../dcl/dcl.init.aggr.md), [[dcl.init.string]](../dcl/dcl.init.string.md)).

**9** If the *expression* in a *noptr-new-declarator* is present, it is implicitly converted to `std::size_t`. The *expression* is erroneous if:

- the expression is of non-class type and its value before converting to `std::size_t` is less than zero;
- the expression is of class type and its value before application of the second standard conversion ([[over.ics.user]](../over/over.ics.user.md))[^1] is less than zero;
- its value is such that the size of the allocated object would exceed the implementation-defined limit ([[implimits]](../implimits/implimits.md)); or
- the *new-initializer* is a *braced-init-list* and the number of array elements for which initializers are provided (including the terminating `'\0'` in a *string-literal* ([[lex.string]](../lex/lex.string.md))) exceeds the number of elements to initialize.

If the *expression* is erroneous after converting to `std::size_t`:

- if the *expression* is a core constant expression, the program is ill-formed;

- otherwise, an allocation function is not called; instead

  - if the allocation function that would have been called has a non-throwing exception specification ([[except.spec]](../except/except.spec.md)), the value of the *new-expression* is the null pointer value of the required result type;
  - otherwise, the *new-expression* terminates by throwing an exception of a type that would match a handler ([[except.handle]](../except/except.handle.md)) of type `std::bad_array_new_length` ([[new.badlength]](../lib/support/new.badlength.md)).

When the value of the *expression* is zero, the allocation function is called to allocate an array with no elements.

**10** A *new-expression* may obtain storage for the object by calling an allocation function ([[basic.stc.dynamic.allocation]](../basic/basic.stc.dynamic.allocation.md)). If the *new-expression* terminates by throwing an exception, it may release storage by calling a deallocation function ([[basic.stc.dynamic.deallocation]](../basic/basic.stc.dynamic.deallocation.md)). If the allocated type is a non-array type, the allocation function’s name is `operator new` and the deallocation function’s name is `operator delete`. If the allocated type is an array type, the allocation function’s name is `operator new[]` and the deallocation function’s name is `operator delete[]`. [*Note 7*: An implementation is required to provide default definitions for the global allocation functions ([[basic.stc.dynamic]](../basic/basic.stc.dynamic.md), [[new.delete.single]](../lib/support/new.delete.single.md), [[new.delete.array]](../lib/support/new.delete.array.md)). A C++ program can provide alternative definitions of these functions ([[replacement.functions]](../lib/library/replacement.functions.md)) and/or class-specific versions ([[class.free]](../class/class.free.md)). The set of allocation and deallocation functions that may be called by a *new-expression* may include functions that do not perform allocation or deallocation; for example, see [[new.delete.placement]](../lib/support/new.delete.placement.md). — *end note*]

**11** If the *new-expression* begins with a unary `::` operator, the allocation function’s name is looked up in the global scope. Otherwise, if the allocated type is a class type `T` or array thereof, the allocation function’s name is looked up in the scope of `T`. If this lookup fails to find the name, or if the allocated type is not a class type, the allocation function’s name is looked up in the global scope.

**12** An implementation is allowed to omit a call to a replaceable global allocation function ([[new.delete.single]](../lib/support/new.delete.single.md), [[new.delete.array]](../lib/support/new.delete.array.md)). When it does so, the storage is instead provided by the implementation or provided by extending the allocation of another *new-expression*.

**13** During an evaluation of a constant expression, a call to an allocation function is always omitted. [*Note 8*: Only *new-expression*s that would otherwise result in a call to a replaceable global allocation function can be evaluated in constant expressions ([[expr.const]](expr.const.md)). — *end note*]

**14** The implementation may extend the allocation of a *new-expression* `e1` to provide storage for a *new-expression* `e2` if the following would be true were the allocation not extended:

- the evaluation of `e1` is sequenced before the evaluation of `e2`, and
- `e2` is evaluated whenever `e1` obtains storage, and
- both `e1` and `e2` invoke the same replaceable global allocation function, and
- if the allocation function invoked by `e1` and `e2` is throwing, any exceptions thrown in the evaluation of either `e1` or `e2` would be first caught in the same handler, and
- the pointer values produced by `e1` and `e2` are operands to evaluated *delete-expression*s, and
- the evaluation of `e2` is sequenced before the evaluation of the *delete-expression* whose operand is the pointer value produced by `e1`.

[*Example 5*:

```cpp
void can_merge(int x) {
  // These allocations are safe for merging:
  std::unique_ptr<char[]> a{new (std::nothrow) char[8]};
  std::unique_ptr<char[]> b{new (std::nothrow) char[8]};
  std::unique_ptr<char[]> c{new (std::nothrow) char[x]};

  g(a.get(), b.get(), c.get());
}

void cannot_merge(int x) {
  std::unique_ptr<char[]> a{new char[8]};
  try {
    // Merging this allocation would change its catch handler.
    std::unique_ptr<char[]> b{new char[x]};
  } catch (const std::bad_alloc& e) {
    std::cerr << "Allocation failed: " << e.what() << std::endl;
    throw;
  }
}
```

— *end example*]

**15** When a *new-expression* calls an allocation function and that allocation has not been extended, the *new-expression* passes the amount of space requested to the allocation function as the first argument of type `std::size_t`. That argument shall be no less than the size of the object being created; it may be greater than the size of the object being created only if the object is an array and the allocation function is not a non-allocating form ([[new.delete.placement]](../lib/support/new.delete.placement.md)). For arrays of `char`, `unsigned char`, and `std::byte`, the difference between the result of the *new-expression* and the address returned by the allocation function shall be an integral multiple of the strictest fundamental alignment requirement ([[basic.align]](../basic/basic.align.md)) of any object type whose size is no greater than the size of the array being created. [*Note 9*: Because allocation functions are assumed to return pointers to storage that is appropriately aligned for objects of any type with fundamental alignment, this constraint on array allocation overhead permits the common idiom of allocating character arrays into which objects of other types will later be placed. — *end note*]

**16** When a *new-expression* calls an allocation function and that allocation has been extended, the size argument to the allocation call shall be no greater than the sum of the sizes for the omitted calls as specified above, plus the size for the extended call had it not been extended, plus any padding necessary to align the allocated objects within the allocated memory.

**17** The *new-placement* syntax is used to supply additional arguments to an allocation function; such an expression is called a *placement *new-expression**.

**18** Overload resolution is performed on a function call created by assembling an argument list. The first argument is the amount of space requested, and has type `std::size_t`. If the type of the allocated object has new-extended alignment, the next argument is the type’s alignment, and has type `std::align_val_t`. If the *new-placement* syntax is used, the *initializer-clause*s in its *expression-list* are the succeeding arguments. If no matching function is found then

- if the allocated object type has new-extended alignment, the alignment argument is removed from the argument list;
- otherwise, an argument that is the type’s alignment and has type `std::align_val_t` is added into the argument list immediately after the first argument;

and then overload resolution is performed again.

**19** [*Example 6*:

- `new T` results in one of the following calls:

  ```cpp
  operator new(sizeof(T))
  operator new(sizeof(T), std::align_val_t(alignof(T)))
  ```

- `new(2,f) T` results in one of the following calls:

  ```cpp
  operator new(sizeof(T), 2, f)
  operator new(sizeof(T), std::align_val_t(alignof(T)), 2, f)
  ```

- `new T[5]` results in one of the following calls:

  ```cpp
  operator new[](sizeof(T) * 5 + x)
  operator new[](sizeof(T) * 5 + x, std::align_val_t(alignof(T)))
  ```

- `new(2,f) T[5]` results in one of the following calls:

  ```cpp
  operator new[](sizeof(T) * 5 + x, 2, f)
  operator new[](sizeof(T) * 5 + x, std::align_val_t(alignof(T)), 2, f)
  ```

Here, each instance of `x` is a non-negative unspecified value representing array allocation overhead; the result of the *new-expression* will be offset by this amount from the value returned by `operator new[]`. This overhead may be applied in all array *new-expression*s, including those referencing a placement allocation function, except when referencing the library function `operator new[](std::size_t, void*)`. The amount of overhead may vary from one invocation of `new` to another. — *end example*]

**20** [*Note 10*: Unless an allocation function has a non-throwing exception specification ([[except.spec]](../except/except.spec.md)), it indicates failure to allocate storage by throwing a `std::bad_alloc` exception ([[basic.stc.dynamic.allocation]](../basic/basic.stc.dynamic.allocation.md), [[except]](../except/except.md), [[bad.alloc]](../lib/support/bad.alloc.md)); it returns a non-null pointer otherwise. If the allocation function has a non-throwing exception specification, it returns null to indicate failure to allocate storage and a non-null pointer otherwise. — *end note*] If the allocation function is a non-allocating form ([[new.delete.placement]](../lib/support/new.delete.placement.md)) that returns null, the behavior is undefined. Otherwise, if the allocation function returns null, initialization shall not be done, the deallocation function shall not be called, and the value of the *new-expression* shall be null.

**21** [*Note 11*: When the allocation function returns a value other than null, it must be a pointer to a block of storage in which space for the object has been reserved. The block of storage is assumed to be appropriately aligned and of the requested size. The address of the created object will not necessarily be the same as that of the block if the object is an array. — *end note*]

**22** A *new-expression* that creates an object of type `T` initializes that object as follows:

- If the *new-initializer* is omitted, the object is default-initialized ([[dcl.init]](../dcl/dcl.init.md)). [*Note 12*: If no initialization is performed, the object has an indeterminate value. — *end note*]
- Otherwise, the *new-initializer* is interpreted according to the initialization rules of [[dcl.init]](../dcl/dcl.init.md) for direct-initialization.

**23** The invocation of the allocation function is sequenced before the evaluations of expressions in the *new-initializer*. Initialization of the allocated object is sequenced before the value computation of the *new-expression*.

**24** If the *new-expression* creates an object or an array of objects of class type, access and ambiguity control are done for the allocation function, the deallocation function ([[class.free]](../class/class.free.md)), and the constructor ([[class.ctor]](../class/class.ctor.md)) selected for the initialization (if any). If the *new-expression* creates an array of objects of class type, the destructor is potentially invoked ([[class.dtor]](../class/class.dtor.md)).

**25** If any part of the object initialization described above[^2] terminates by throwing an exception and a suitable deallocation function can be found, the deallocation function is called to free the memory in which the object was being constructed, after which the exception continues to propagate in the context of the *new-expression*. If no unambiguous matching deallocation function can be found, propagating the exception does not cause the object’s memory to be freed. [*Note 13*: This is appropriate when the called allocation function does not allocate memory; otherwise, it is likely to result in a memory leak. — *end note*]

**26** If the *new-expression* begins with a unary `::` operator, the deallocation function’s name is looked up in the global scope. Otherwise, if the allocated type is a class type `T` or an array thereof, the deallocation function’s name is looked up in the scope of `T`. If this lookup fails to find the name, or if the allocated type is not a class type or array thereof, the deallocation function’s name is looked up in the global scope.

**27** A declaration of a placement deallocation function matches the declaration of a placement allocation function if it has the same number of parameters and, after parameter transformations ([[dcl.fct]](../dcl/dcl.fct.md)), all parameter types except the first are identical. If the lookup finds a single matching deallocation function, that function will be called; otherwise, no deallocation function will be called. If the lookup finds a usual deallocation function and that function, considered as a placement deallocation function, would have been selected as a match for the allocation function, the program is ill-formed. For a non-placement allocation function, the normal deallocation function lookup is used to find the matching deallocation function ([[expr.delete]](expr.delete.md)). [*Example 7*:

```cpp
struct S {
  // Placement allocation function:
  static void* operator new(std::size_t, std::size_t);

  // Usual (non-placement) deallocation function:
  static void operator delete(void*, std::size_t);
};

S* p = new (0) S;   // error: non-placement deallocation function matches
                    // placement allocation function
```

— *end example*]

**28** If a *new-expression* calls a deallocation function, it passes the value returned from the allocation function call as the first argument of type `void*`. If a placement deallocation function is called, it is passed the same additional arguments as were passed to the placement allocation function, that is, the same arguments as those specified with the *new-placement* syntax. If the implementation is allowed to introduce a temporary object or make a copy of any argument as part of the call to the allocation function, it is unspecified whether the same object is used in the call to both the allocation and deallocation functions.

[^1]: If the conversion function returns a signed integer type, the second standard conversion converts to the unsigned type `std::size_t` and thus thwarts any attempt to detect a negative value afterwards.
[^2]: This may include evaluating a *new-initializer* and/or calling a constructor.
