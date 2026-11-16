# 6.8 Types [basic.types]

[Contents](README.md) · up: [6 Basics \[basic\]](basic.md) · prev: [6.7.7 Temporary objects \[class.temporary\]](class.temporary.md) · next: [6.8.1 Fundamental types \[basic.fundamental\]](basic.fundamental.md)

**1** [*Note 1*: [[basic.types]](basic.types.md) and the subclauses thereof impose requirements on implementations regarding the representation of types. There are two kinds of types: fundamental types and compound types. Types describe objects ([[intro.object]](intro.object.md)), references ([[dcl.ref]](dcl.ref.md)), or functions ([[dcl.fct]](dcl.fct.md)). — *end note*]

**2** For any object (other than a potentially-overlapping subobject) of trivially copyable type `T`, whether or not the object holds a valid value of type `T`, the underlying bytes ([[intro.memory]](intro.memory.md)) making up the object can be copied into an array of `char`, `unsigned char`, or `std::byte` ([[cstddef.syn]](lib/cstddef.syn.md)).[^1] If the content of that array is copied back into the object, the object shall subsequently hold its original value. [*Example 1*:

```cpp
constexpr std::size_t N = sizeof(T);
char buf[N];
T obj;                          // obj initialized to its original value
std::memcpy(buf, &obj, N);      // between these two calls to std::memcpy, obj might be modified
std::memcpy(&obj, buf, N);      // at this point, each subobject of obj of scalar type holds its original value
```

— *end example*]

**3** For any trivially copyable type `T`, if two pointers to `T` point to distinct `T` objects `obj1` and `obj2`, where neither `obj1` nor `obj2` is a potentially-overlapping subobject, if the underlying bytes ([[intro.memory]](intro.memory.md)) making up `obj1` are copied into `obj2`,[^2] `obj2` shall subsequently hold the same value as `obj1`. [*Example 2*:

```cpp
T* t1p;
T* t2p;
    // provided that t2p points to an initialized object ...
std::memcpy(t1p, t2p, sizeof(T));
    // at this point, every subobject of trivially copyable type in *t1p contains
    // the same value as the corresponding subobject in *t2p
```

— *end example*]

**4** The *object representation* of an object of type `T` is the sequence of *N* `unsigned char` objects taken up by the object of type `T`, where *N* equals `sizeof(T)`. The *value representation* of an object of type `T` is the set of bits that participate in representing a value of type `T`. Bits in the object representation that are not part of the value representation are *padding bits*. For trivially copyable types, the value representation is a set of bits in the object representation that determines a *value*, which is one discrete element of an implementation-defined set of values.[^3]

**5** A class that has been declared but not defined, an enumeration type in certain contexts ([[dcl.enum]](dcl.enum.md)), or an array of unknown bound or of incomplete element type, is an *incompletely-defined object type*.[^4] Incompletely-defined object types and *cv* `void` are *incomplete types* ([[basic.fundamental]](basic.fundamental.md)). Objects shall not be defined to have an incomplete type.

**6** A class type (such as “`class X`”) might be incomplete at one point in a translation unit and complete later on; the type “`class X`” is the same type at both points. The declared type of an array object might be an array of incomplete class type and therefore incomplete; if the class type is completed later on in the translation unit, the array type becomes complete; the array type at those two points is the same type. The declared type of an array object might be an array of unknown bound and therefore be incomplete at one point in a translation unit and complete later on; the array types at those two points (“array of unknown bound of `T`” and “array of `N` `T`”) are different types. The type of a pointer to array of unknown bound, or of a type defined by a `typedef` declaration to be an array of unknown bound, cannot be completed. [*Example 3*:

```cpp
class X;                        // X is an incomplete type
extern X* xp;                   // xp is a pointer to an incomplete type
extern int arr[];               // the type of arr is incomplete
typedef int UNKA[];             // UNKA is an incomplete type
UNKA* arrp;                     // arrp is a pointer to an incomplete type
UNKA** arrpp;

void foo() {
  xp++;                         // error: X is incomplete
  arrp++;                       // error: incomplete type
  arrpp++;                      // OK: sizeof UNKA* is known
}

struct X { int i; };            // now X is a complete type
int  arr[10];                   // now the type of arr is complete

X x;
void bar() {
  xp = &x;                      // OK; type is ``pointer to X''
  arrp = &arr;                  // error: different types
  xp++;                         // OK: X is complete
  arrp++;                       // error: UNKA can't be completed
}
```

— *end example*]

**7** [*Note 2*: The rules for declarations and expressions describe in which contexts incomplete types are prohibited. — *end note*]

**8** An *object type* is a (possibly cv-qualified) type that is not a function type, not a reference type, and not *cv* `void`.

**9** Arithmetic types ([[basic.fundamental]](basic.fundamental.md)), enumeration types, pointer types, pointer-to-member types ([[basic.compound]](basic.compound.md)), `std::nullptr_t`, and cv-qualified ([[basic.type.qualifier]](basic.type.qualifier.md)) versions of these types are collectively called *scalar types*. Scalar types, trivially copyable class types ([[class.prop]](class.prop.md)), arrays of such types, and cv-qualified versions of these types are collectively called *trivially copyable types*. Scalar types, trivial class types ([[class.prop]](class.prop.md)), arrays of such types and cv-qualified versions of these types are collectively called *trivial types*. Scalar types, standard-layout class types ([[class.prop]](class.prop.md)), arrays of such types and cv-qualified versions of these types are collectively called *standard-layout types*. Scalar types, implicit-lifetime class types ([[class.prop]](class.prop.md)), array types, and cv-qualified versions of these types are collectively called *implicit-lifetime types*.

**10** A type is a *literal type* if it is:

- *cv* `void`; or

- a scalar type; or

- a reference type; or

- an array of literal type; or

- a possibly cv-qualified class type ([[class]](class.md)) that has all of the following properties:

  - it has a constexpr destructor ([[dcl.constexpr]](dcl.constexpr.md)),
  - it is either a closure type ([[expr.prim.lambda.closure]](expr.prim.lambda.closure.md)), an aggregate type ([[dcl.init.aggr]](dcl.init.aggr.md)), or has at least one constexpr constructor or constructor template (possibly inherited ([[namespace.udecl]](namespace.udecl.md)) from a base class) that is not a copy or move constructor,
  - if it is a union, at least one of its non-static data members is of non-volatile literal type, and
  - if it is not a union, all of its non-static data members and base classes are of non-volatile literal types.

[*Note 3*: A literal type is one for which it might be possible to create an object within a constant expression. It is not a guarantee that it is possible to create such an object, nor is it a guarantee that any object of that type will be usable in a constant expression. — *end note*]

**11** Two types *cv1* `T1` and *cv2* `T2` are *layout-compatible* types if `T1` and `T2` are the same type, layout-compatible enumerations ([[dcl.enum]](dcl.enum.md)), or layout-compatible standard-layout class types ([[class.mem]](class.mem.md)).

## Subclauses

- [6.8.1 Fundamental types \[basic.fundamental\]](basic.fundamental.md)
- [6.8.2 Compound types \[basic.compound\]](basic.compound.md)
- [6.8.3 CV-qualifiers \[basic.type.qualifier\]](basic.type.qualifier.md)
- [6.8.4 Integer conversion rank \[conv.rank\]](conv.rank.md)

[^1]: By using, for example, the library functions ([[headers]](lib/headers.md)) `std::memcpy` or `std::memmove`.
[^2]: By using, for example, the library functions ([[headers]](lib/headers.md)) `std::memcpy` or `std::memmove`.
[^3]: The intent is that the memory model of C++ is compatible with that of ISO/IEC 9899 Programming Language C.
[^4]: The size and layout of an instance of an incompletely-defined object type is unknown.
