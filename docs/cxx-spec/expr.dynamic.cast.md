# 7.6.1.6 Dynamic cast [expr.dynamic.cast]

[Contents](README.md) · up: [7.6.1 Postfix expressions \[expr.post\]](expr.post.md) · prev: [7.6.1.5 Increment and decrement \[expr.post.incr\]](expr.post.incr.md) · next: [7.6.1.7 Type identification \[expr.typeid\]](expr.typeid.md)

**1** The result of the expression `dynamic_cast<T>(v)` is the result of converting the expression `v` to type `T`. `T` shall be a pointer or reference to a complete class type, or “pointer to *cv* `void`”. The `dynamic_cast` operator shall not cast away constness ([[expr.const.cast]](expr.const.cast.md)).

**2** If `T` is a pointer type, `v` shall be a prvalue of a pointer to complete class type, and the result is a prvalue of type `T`. If `T` is an lvalue reference type, `v` shall be an lvalue of a complete class type, and the result is an lvalue of the type referred to by `T`. If `T` is an rvalue reference type, `v` shall be a glvalue having a complete class type, and the result is an xvalue of the type referred to by `T`.

**3** If the type of `v` is the same as `T` (ignoring cv-qualifications), the result is `v` (converted if necessary).

**4** If `T` is “pointer to *cv1* `B`” and `v` has type “pointer to *cv2* `D`” such that `B` is a base class of `D`, the result is a pointer to the unique `B` subobject of the `D` object pointed to by `v`, or a null pointer value if `v` is a null pointer value. Similarly, if `T` is “reference to *cv1* `B`” and `v` has type *cv2* `D` such that `B` is a base class of `D`, the result is the unique `B` subobject of the `D` object referred to by `v`.[^1] In both the pointer and reference cases, the program is ill-formed if `B` is an inaccessible or ambiguous base class of `D`. [*Example 1*:

```cpp
struct B { };
struct D : B { };
void foo(D* dp) {
  B*  bp = dynamic_cast<B*>(dp);    // equivalent to B* bp = dp;
}
```

— *end example*]

**5** Otherwise, `v` shall be a pointer to or a glvalue of a polymorphic type ([[class.virtual]](class.virtual.md)).

**6** If `v` is a null pointer value, the result is a null pointer value.

**7** If `T` is “pointer to *cv* `void`”, then the result is a pointer to the most derived object pointed to by `v`. Otherwise, a runtime check is applied to see if the object pointed or referred to by `v` can be converted to the type pointed or referred to by `T`.

**8** If `C` is the class type to which `T` points or refers, the runtime check logically executes as follows:

- If, in the most derived object pointed (referred) to by `v`, `v` points (refers) to a public base class subobject of a `C` object, and if only one object of type `C` is derived from the subobject pointed (referred) to by `v` the result points (refers) to that `C` object.
- Otherwise, if `v` points (refers) to a public base class subobject of the most derived object, and the type of the most derived object has a base class, of type `C`, that is unambiguous and public, the result points (refers) to the `C` subobject of the most derived object.
- Otherwise, the runtime check *fails*.

**9** The value of a failed cast to pointer type is the null pointer value of the required result type. A failed cast to reference type throws an exception ([[except.throw]](except.throw.md)) of a type that would match a handler ([[except.handle]](except.handle.md)) of type `std::bad_cast` ([[bad.cast]](lib/bad.cast.md)).

[*Example 2*:

```cpp
class A { virtual void f(); };
class B { virtual void g(); };
class D : public virtual A, private B { };
void g() {
  D   d;
  B*  bp = (B*)&d;                  // cast needed to break protection
  A*  ap = &d;                      // public derivation, no cast needed
  D&  dr = dynamic_cast<D&>(*bp);   // fails
  ap = dynamic_cast<A*>(bp);        // fails
  bp = dynamic_cast<B*>(ap);        // fails
  ap = dynamic_cast<A*>(&d);        // succeeds
  bp = dynamic_cast<B*>(&d);        // ill-formed (not a runtime check)
}

class E : public D, public B { };
class F : public E, public D { };
void h() {
  F   f;
  A*  ap  = &f;                     // succeeds: finds unique A
  D*  dp  = dynamic_cast<D*>(ap);   // fails: yields null; f has two D subobjects
  E*  ep  = (E*)ap;                 // error: cast from virtual base
  E*  ep1 = dynamic_cast<E*>(ap);   // succeeds
}
```

— *end example*] [*Note 1*: Subclause [[class.cdtor]](class.cdtor.md) describes the behavior of a `dynamic_cast` applied to an object under construction or destruction. — *end note*]

[^1]: The most derived object ([[intro.object]](intro.object.md)) pointed or referred to by `v` can contain other `B` objects as base classes, but these are ignored.
