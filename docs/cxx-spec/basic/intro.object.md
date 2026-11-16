# 6.7.2 Object model [intro.object]

[Contents](../README.md) · up: [6.7 Memory and objects \[basic.memobj\]](basic.memobj.md) · prev: [6.7.1 Memory model \[intro.memory\]](intro.memory.md) · next: [6.7.3 Lifetime \[basic.life\]](basic.life.md)

**1** The constructs in a C++ program create, destroy, refer to, access, and manipulate objects. An *object* is created by a definition ([[basic.def]](basic.def.md)), by a *new-expression* ([[expr.new]](../expr/expr.new.md)), by an operation that implicitly creates objects (see below), when implicitly changing the active member of a union ([[class.union]](../class/class.union.md)), or when a temporary object is created ([[conv.rval]](../expr/conv.rval.md), [[class.temporary]](class.temporary.md)). An object occupies a region of storage in its period of construction ([[class.cdtor]](../class/class.cdtor.md)), throughout its lifetime ([[basic.life]](basic.life.md)), and in its period of destruction ([[class.cdtor]](../class/class.cdtor.md)). [*Note 1*: A function is not an object, regardless of whether or not it occupies storage in the way that objects do. — *end note*] The properties of an object are determined when the object is created. An object can have a name ([[basic.pre]](basic.pre.md)). An object has a storage duration ([[basic.stc]](basic.stc.md)) which influences its lifetime ([[basic.life]](basic.life.md)). An object has a type ([[basic.types]](basic.types.md)). Some objects are polymorphic ([[class.virtual]](../class/class.virtual.md)); the implementation generates information associated with each such object that makes it possible to determine that object’s type during program execution. For other objects, the interpretation of the values found therein is determined by the type of the *expression*s ([[expr.compound]](../expr/expr.compound.md)) used to access them.

**2** Objects can contain other objects, called *subobjects*. A subobject can be a *member subobject* ([[class.mem]](../class/class.mem.md)), a *base class subobject* ([[class.derived]](../class/class.derived.md)), or an array element. An object that is not a subobject of any other object is called a *complete object*. If an object is created in storage associated with a member subobject or array element *e* (which may or may not be within its lifetime), the created object is a subobject of *e*’s containing object if:

- the lifetime of *e*’s containing object has begun and not ended, and
- the storage for the new object exactly overlays the storage location associated with *e*, and
- the new object is of the same type as *e* (ignoring cv-qualification).

**3** If a complete object is created ([[expr.new]](../expr/expr.new.md)) in storage associated with another object *e* of type “array of *N* `unsigned char`” or of type “array of *N* `std::byte`” ([[cstddef.syn]](../lib/support/cstddef.syn.md)), that array *provides storage* for the created object if:

- the lifetime of *e* has begun and not ended, and
- the storage for the new object fits entirely within *e*, and
- there is no smaller array object that satisfies these constraints.

[*Note 2*: If that portion of the array previously provided storage for another object, the lifetime of that object ends because its storage was reused ([[basic.life]](basic.life.md)). — *end note*] [*Example 1*:

```cpp
template<typename ...T>
struct AlignedUnion {
  alignas(T...) unsigned char data[max(sizeof(T)...)];
};
int f() {
  AlignedUnion<int, char> au;
  int *p = new (au.data) int;           // OK, au.data provides storage
  char *c = new (au.data) char();       // OK, ends lifetime of *p
  char *d = new (au.data + 1) char();
  return *c + *d;                       // OK
}

struct A { unsigned char a[32]; };
struct B { unsigned char b[16]; };
A a;
B *b = new (a.a + 8) B;                 // a.a provides storage for *b
int *p = new (b->b + 4) int;            // b->b provides storage for *p
                                        // a.a does not provide storage for *p (directly),
                                        // but *p is nested within a (see below)
```

— *end example*]

**4** An object *a* is *nested within* another object *b* if:

- *a* is a subobject of *b*, or
- *b* provides storage for *a*, or
- there exists an object *c* where *a* is nested within *c*, and *c* is nested within *b*.

**5** For every object `x`, there is some object called the *complete object of* `x`, determined as follows:

- If `x` is a complete object, then the complete object of `x` is itself.
- Otherwise, the complete object of `x` is the complete object of the (unique) object that contains `x`.

**6** If a complete object, a data member ([[class.mem]](../class/class.mem.md)), or an array element is of class type, its type is considered the *most derived class*, to distinguish it from the class type of any base class subobject; an object of a most derived class type or of a non-class type is called a *most derived object*.

**7** A *potentially-overlapping subobject* is either:

- a base class subobject, or
- a non-static data member declared with the `no_unique_address` attribute ([[dcl.attr.nouniqueaddr]](../dcl/dcl.attr.nouniqueaddr.md)).

**8** An object has nonzero size if it

- is not a potentially-overlapping subobject, or
- is not of class type, or
- is of a class type with virtual member functions or virtual base classes, or
- has subobjects of nonzero size or bit-fields of nonzero length.

Otherwise, if the object is a base class subobject of a standard-layout class type with no non-static data members, it has zero size. Otherwise, the circumstances under which the object has zero size are implementation-defined. Unless it is a bit-field ([[class.bit]](../class/class.bit.md)), an object with nonzero size shall occupy one or more bytes of storage, including every byte that is occupied in full or in part by any of its subobjects. An object of trivially copyable or standard-layout type ([[basic.types]](basic.types.md)) shall occupy contiguous bytes of storage.

**9** Unless an object is a bit-field or a subobject of zero size, the address of that object is the address of the first byte it occupies. Two objects with overlapping lifetimes that are not bit-fields may have the same address if one is nested within the other, or if at least one is a subobject of zero size and they are of different types; otherwise, they have distinct addresses and occupy disjoint bytes of storage.[^1] [*Example 2*:

```cpp
static const char test1 = 'x';
static const char test2 = 'x';
const bool b = &test1 != &test2;        // always true
```

— *end example*] The address of a non-bit-field subobject of zero size is the address of an unspecified byte of storage occupied by the complete object of that subobject.

**10** Some operations are described as *implicitly creating objects* within a specified region of storage. For each operation that is specified as implicitly creating objects, that operation implicitly creates and starts the lifetime of zero or more objects of implicit-lifetime types ([[basic.types]](basic.types.md)) in its specified region of storage if doing so would result in the program having defined behavior. If no such set of objects would give the program defined behavior, the behavior of the program is undefined. If multiple such sets of objects would give the program defined behavior, it is unspecified which such set of objects is created. [*Note 3*: Such operations do not start the lifetimes of subobjects of such objects that are not themselves of implicit-lifetime types. — *end note*]

**11** Further, after implicitly creating objects within a specified region of storage, some operations are described as producing a pointer to a *suitable created object*. These operations select one of the implicitly-created objects whose address is the address of the start of the region of storage, and produce a pointer value that points to that object, if that value would result in the program having defined behavior. If no such pointer value would give the program defined behavior, the behavior of the program is undefined. If multiple such pointer values would give the program defined behavior, it is unspecified which such pointer value is produced.

**12** [*Example 3*:

```cpp
#include <cstdlib>
struct X { int a, b; };
X *make_x() {
  // The call to std::malloc implicitly creates an object of type X
  // and its subobjects a and b, and returns a pointer to that X object
  // (or an object that is pointer-interconvertible ([basic.compound]) with it),
  // in order to give the subsequent class member access operations
  // defined behavior.
  X *p = (X*)std::malloc(sizeof(struct X));
  p->a = 1;
  p->b = 2;
  return p;
}
```

— *end example*]

**13** An operation that begins the lifetime of an array of `char`, `unsigned char`, or `std::byte` implicitly creates objects within the region of storage occupied by the array. [*Note 4*: The array object provides storage for these objects. — *end note*] Any implicit or explicit invocation of a function named `operator new` or `operator new[]` implicitly creates objects in the returned region of storage and returns a pointer to a suitable created object. [*Note 5*: Some functions in the C++ standard library implicitly create objects ([[allocator.traits.members]](../lib/utilities/allocator.traits.members.md), [[c.malloc]](../lib/utilities/c.malloc.md), [[cstring.syn]](../lib/strings/cstring.syn.md), [[bit.cast]](../lib/numerics/bit.cast.md)). — *end note*]

[^1]: Under the “as-if” rule an implementation is allowed to store two objects at the same machine address or not store an object at all if the program cannot observe the difference ([[intro.execution]](intro.execution.md)).
