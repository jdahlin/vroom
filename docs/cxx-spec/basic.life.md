# 6.7.3 Lifetime [basic.life]

[Contents](README.md) · up: [6.7 Memory and objects \[basic.memobj\]](basic.memobj.md) · prev: [6.7.2 Object model \[intro.object\]](intro.object.md) · next: [6.7.4 Indeterminate values \[basic.indet\]](basic.indet.md)

**1** The *lifetime* of an object or reference is a runtime property of the object or reference. A variable is said to have *vacuous initialization* if it is default-initialized and, if it is of class type or a (possibly multi-dimensional) array thereof, that class type has a trivial default constructor. The lifetime of an object of type `T` begins when:

- storage with the proper alignment and size for type `T` is obtained, and
- its initialization (if any) is complete (including vacuous initialization) ([[dcl.init]](dcl.init.md)),

except that if the object is a union member or subobject thereof, its lifetime only begins if that union member is the initialized member in the union ([[dcl.init.aggr]](dcl.init.aggr.md), [[class.base.init]](class.base.init.md)), or as described in [[class.union]](class.union.md) and [[class.copy.ctor]](class.copy.ctor.md), and except as described in [[allocator.members]](lib/allocator.members.md). The lifetime of an object *o* of type `T` ends when:

- if `T` is a non-class type, the object is destroyed, or
- if `T` is a class type, the destructor call starts, or
- the storage which the object occupies is released, or is reused by an object that is not nested within *o* ([[intro.object]](intro.object.md)).

**2** The lifetime of a reference begins when its initialization is complete. The lifetime of a reference ends as if it were a scalar object requiring storage.

**3** [*Note 1*: [[class.base.init]](class.base.init.md) describes the lifetime of base and member subobjects. — *end note*]

**4** The properties ascribed to objects and references throughout this document apply for a given object or reference only during its lifetime. [*Note 2*: In particular, before the lifetime of an object starts and after its lifetime ends there are significant restrictions on the use of the object, as described below, in [[class.base.init]](class.base.init.md) and in [[class.cdtor]](class.cdtor.md). Also, the behavior of an object under construction and destruction might not be the same as the behavior of an object whose lifetime has started and not ended. [[class.base.init]](class.base.init.md) and [[class.cdtor]](class.cdtor.md) describe the behavior of an object during its periods of construction and destruction. — *end note*]

**5** A program may end the lifetime of any object by reusing the storage which the object occupies or by explicitly calling a destructor or pseudo-destructor ([[expr.prim.id.dtor]](expr.prim.id.dtor.md)) for the object. For an object of a class type, the program is not required to call the destructor explicitly before the storage which the object occupies is reused or released; however, if there is no explicit call to the destructor or if a *delete-expression* ([[expr.delete]](expr.delete.md)) is not used to release the storage, the destructor is not implicitly called and any program that depends on the side effects produced by the destructor has undefined behavior.

**6** Before the lifetime of an object has started but after the storage which the object will occupy has been allocated[^1] or, after the lifetime of an object has ended and before the storage which the object occupied is reused or released, any pointer that represents the address of the storage location where the object will be or was located may be used but only in limited ways. For an object under construction or destruction, see [[class.cdtor]](class.cdtor.md). Otherwise, such a pointer refers to allocated storage ([[basic.stc.dynamic.allocation]](basic.stc.dynamic.allocation.md)), and using the pointer as if the pointer were of type `void*` is well-defined. Indirection through such a pointer is permitted but the resulting lvalue may only be used in limited ways, as described below. The program has undefined behavior if:

- the object will be or was of a class type with a non-trivial destructor and the pointer is used as the operand of a *delete-expression*,
- the pointer is used to access a non-static data member or call a non-static member function of the object, or
- the pointer is implicitly converted ([[conv.ptr]](conv.ptr.md)) to a pointer to a virtual base class, or
- the pointer is used as the operand of a `static_cast` ([[expr.static.cast]](expr.static.cast.md)), except when the conversion is to pointer to *cv* `void`, or to pointer to *cv* `void` and subsequently to pointer to *cv* `char`, *cv* `unsigned char`, or *cv* `std::byte` ([[cstddef.syn]](lib/cstddef.syn.md)), or
- the pointer is used as the operand of a `dynamic_cast` ([[expr.dynamic.cast]](expr.dynamic.cast.md)).

[*Example 1*:

```cpp
#include <cstdlib>

struct B {
  virtual void f();
  void mutate();
  virtual ~B();
};

struct D1 : B { void f(); };
struct D2 : B { void f(); };

void B::mutate() {
  new (this) D2;    // reuses storage --- ends the lifetime of *this
  f();              // undefined behavior
  ... = this;       // OK, this points to valid memory
}

void g() {
  void* p = std::malloc(sizeof(D1) + sizeof(D2));
  B* pb = new (p) D1;
  pb->mutate();
  *pb;              // OK: pb points to valid memory
  void* q = pb;     // OK: pb points to valid memory
  pb->f();          // undefined behavior: lifetime of *pb has ended
}
```

— *end example*]

**7** Similarly, before the lifetime of an object has started but after the storage which the object will occupy has been allocated or, after the lifetime of an object has ended and before the storage which the object occupied is reused or released, any glvalue that refers to the original object may be used but only in limited ways. For an object under construction or destruction, see [[class.cdtor]](class.cdtor.md). Otherwise, such a glvalue refers to allocated storage ([[basic.stc.dynamic.allocation]](basic.stc.dynamic.allocation.md)), and using the properties of the glvalue that do not depend on its value is well-defined. The program has undefined behavior if:

- the glvalue is used to access the object, or
- the glvalue is used to call a non-static member function of the object, or
- the glvalue is bound to a reference to a virtual base class ([[dcl.init.ref]](dcl.init.ref.md)), or
- the glvalue is used as the operand of a `dynamic_cast` ([[expr.dynamic.cast]](expr.dynamic.cast.md)) or as the operand of `typeid`.

**8** If, after the lifetime of an object has ended and before the storage which the object occupied is reused or released, a new object is created at the storage location which the original object occupied, a pointer that pointed to the original object, a reference that referred to the original object, or the name of the original object will automatically refer to the new object and, once the lifetime of the new object has started, can be used to manipulate the new object, if the original object is transparently replaceable (see below) by the new object. An object *o*<sub>1</sub> is *transparently replaceable* by an object *o*<sub>2</sub> if:

- the storage that *o*<sub>2</sub> occupies exactly overlays the storage that *o*<sub>1</sub> occupied, and
- *o*<sub>1</sub> and *o*<sub>2</sub> are of the same type (ignoring the top-level cv-qualifiers), and
- *o*<sub>1</sub> is not a complete const object, and
- neither *o*<sub>1</sub> nor *o*<sub>2</sub> is a potentially-overlapping subobject ([[intro.object]](intro.object.md)), and
- either *o*<sub>1</sub> and *o*<sub>2</sub> are both complete objects, or *o*<sub>1</sub> and *o*<sub>2</sub> are direct subobjects of objects *p*<sub>1</sub> and *p*<sub>2</sub>, respectively, and *p*<sub>1</sub> is transparently replaceable by *p*<sub>2</sub>.

[*Example 2*:

```cpp
struct C {
  int i;
  void f();
  const C& operator=( const C& );
};

const C& C::operator=( const C& other) {
  if ( this != &other ) {
    this->~C();                 // lifetime of *this ends
    new (this) C(other);        // new object of type C created
    f();                        // well-defined
  }
  return *this;
}

C c1;
C c2;
c1 = c2;                        // well-defined
c1.f();                         // well-defined; c1 refers to a new object of type C
```

— *end example*] [*Note 3*: If these conditions are not met, a pointer to the new object can be obtained from a pointer that represents the address of its storage by calling `std::launder` ([[ptr.launder]](lib/ptr.launder.md)). — *end note*]

**9** If a program ends the lifetime of an object of type `T` with static ([[basic.stc.static]](basic.stc.static.md)), thread ([[basic.stc.thread]](basic.stc.thread.md)), or automatic ([[basic.stc.auto]](basic.stc.auto.md)) storage duration and if `T` has a non-trivial destructor,[^2] the program must ensure that an object of the original type occupies that same storage location when the implicit destructor call takes place; otherwise the behavior of the program is undefined. This is true even if the block is exited with an exception. [*Example 3*:

```cpp
class T { };
struct B {
   ~B();
};

void h() {
   B b;
   new (&b) T;
}                               // undefined behavior at block exit
```

— *end example*]

**10** Creating a new object within the storage that a const complete object with static, thread, or automatic storage duration occupies, or within the storage that such a const object used to occupy before its lifetime ended, results in undefined behavior. [*Example 4*:

```cpp
struct B {
  B();
  ~B();
};

const B b;

void h() {
  b.~B();
  new (const_cast<B*>(&b)) const B;     // undefined behavior
}
```

— *end example*]

**11** In this subclause, “before” and “after” refer to the “happens before” relation ([[intro.multithread]](intro.multithread.md)). [*Note 4*: Therefore, undefined behavior results if an object that is being constructed in one thread is referenced from another thread without adequate synchronization. — *end note*]

[^1]: For example, before the construction of a global object that is initialized via a user-provided constructor ([[class.cdtor]](class.cdtor.md)).
[^2]: That is, an object for which a destructor will be called implicitly—upon exit from the block for an object with automatic storage duration, upon exit from the thread for an object with thread storage duration, or upon exit from the program for an object with static storage duration.
