# 11.12 Free store [class.free]

[Contents](../README.md) · up: [11 Classes \[class\]](class.md) · prev: [11.11.4 Secondary comparison operators \[class.compare.secondary\]](class.compare.secondary.md) · next: [12 Overloading \[over\]](../over/over.md)

**1** Any allocation function for a class `T` is a static member (even if not explicitly declared `static`).

**2** [*Example 1*:

```cpp
class Arena;
struct B {
  void* operator new(std::size_t, Arena*);
};
struct D1 : B {
};

Arena*  ap;
void foo(int i) {
  new (ap) D1;      // calls B::operator new(std::size_t, Arena*)
  new D1[i];        // calls ::operator new[](std::size_t)
  new D1;           // error: ::operator new(std::size_t) hidden
}
```

— *end example*]

**3** When an object is deleted with a *delete-expression* ([[expr.delete]](../expr/expr.delete.md)), a deallocation function (`operator delete()` for non-array objects or `operator delete[]()` for arrays) is (implicitly) called to reclaim the storage occupied by the object ([[basic.stc.dynamic.deallocation]](../basic/basic.stc.dynamic.deallocation.md)).

**4** Class-specific deallocation function lookup is a part of general deallocation function lookup ([[expr.delete]](../expr/expr.delete.md)) and occurs as follows. If the *delete-expression* is used to deallocate a class object whose static type has a virtual destructor, the deallocation function is the one selected at the point of definition of the dynamic type’s virtual destructor ([[class.dtor]](class.dtor.md)).[^1] Otherwise, if the *delete-expression* is used to deallocate an object of class `T` or array thereof, the deallocation function’s name is looked up in the scope of `T`. If this lookup fails to find the name, general deallocation function lookup ([[expr.delete]](../expr/expr.delete.md)) continues. If the result of the lookup is ambiguous or inaccessible, or if the lookup selects a placement deallocation function, the program is ill-formed.

**5** Any deallocation function for a class `X` is a static member (even if not explicitly declared `static`). [*Example 2*:

```cpp
class X {
  void operator delete(void*);
  void operator delete[](void*, std::size_t);
};

class Y {
  void operator delete(void*, std::size_t);
  void operator delete[](void*);
};
```

— *end example*]

**6** Since member allocation and deallocation functions are `static` they cannot be virtual. [*Note 1*: However, when the *cast-expression* of a *delete-expression* refers to an object of class type, because the deallocation function actually called is looked up in the scope of the class that is the dynamic type of the object if the destructor is virtual, the effect is the same in that case. For example,

```cpp
struct B {
  virtual ~B();
  void operator delete(void*, std::size_t);
};

struct D : B {
  void operator delete(void*);
};

struct E : B {
  void log_deletion();
  void operator delete(E *p, std::destroying_delete_t) {
    p->log_deletion();
    p->~E();
    ::operator delete(p);
  }
};

void f() {
  B* bp = new D;
  delete bp;        // 1: uses D::operator delete(void*)
  bp = new E;
  delete bp;        // 2: uses E::operator delete(E*, std::destroying_delete_t)
}
```

Here, storage for the object of class `D` is deallocated by `D::operator delete()`, and the object of class `E` is destroyed and its storage is deallocated by `E::operator delete()`, due to the virtual destructor. — *end note*] [*Note 2*: Virtual destructors have no effect on the deallocation function actually called when the *cast-expression* of a *delete-expression* refers to an array of objects of class type. For example,

```cpp
struct B {
  virtual ~B();
  void operator delete[](void*, std::size_t);
};

struct D : B {
  void operator delete[](void*, std::size_t);
};

void f(int i) {
  D* dp = new D[i];
  delete [] dp;     // uses D::operator delete[](void*, std::size_t)
  B* bp = new D[i];
  delete[] bp;      // undefined behavior
}
```

— *end note*]

**7** Access to the deallocation function is checked statically. Hence, even though a different one might actually be executed, the statically visible deallocation function is required to be accessible. [*Example 3*: For the call on line “// 1” above, if `B::operator delete()` had been private, the delete expression would have been ill-formed. — *end example*]

**8** [*Note 3*: If a deallocation function has no explicit *noexcept-specifier*, it has a non-throwing exception specification ([[except.spec]](../except/except.spec.md)). — *end note*]

[^1]: A similar provision is not needed for the array version of `operator` `delete` because [[expr.delete]](../expr/expr.delete.md) requires that in this situation, the static type of the object to be deleted be the same as its dynamic type.
