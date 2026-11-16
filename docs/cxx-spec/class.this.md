# 11.4.2.1 The `this` pointer [class.this]

[Contents](README.md) · up: [11.4.2 Non-static member functions \[class.mfct.non-static\]](class.mfct.non-static.md) · prev: [11.4.2 Non-static member functions \[class.mfct.non-static\]](class.mfct.non-static.md) · next: [11.4.3 Special member functions \[special\]](special.md)

**1** In the body of a non-static ([[class.mfct]](class.mfct.md)) member function, the keyword `this` is a prvalue whose value is a pointer to the object for which the function is called. The type of `this` in a member function whose type has a *cv-qualifier-seq* *cv* and whose class is `X` is “pointer to *cv* `X`”. [*Note 1*: Thus in a const member function, the object for which the function is called is accessed through a const access path. — *end note*] [*Example 1*:

```cpp
struct s {
  int a;
  int f() const;
  int g() { return a++; }
  int h() const { return a++; } // error
};

int s::f() const { return a; }
```

The `a++` in the body of `s::h` is ill-formed because it tries to modify (a part of) the object for which `s::h()` is called. This is not allowed in a const member function because `this` is a pointer to `const`; that is, `*this` has `const` type. — *end example*]

**2** [*Note 2*: Similarly, `volatile` semantics ([[dcl.type.cv]](dcl.type.cv.md)) apply in volatile member functions when accessing the object and its non-static data members. — *end note*]

**3** A member function whose type has a *cv-qualifier-seq* *cv1* can be called on an object expression ([[expr.ref]](expr.ref.md)) of type *cv2* `T` only if *cv1* is the same as or more cv-qualified than *cv2* ([[basic.type.qualifier]](basic.type.qualifier.md)). [*Example 2*:

```cpp
void k(s& x, const s& y) {
  x.f();
  x.g();
  y.f();
  y.g();                        // error
}
```

The call `y.g()` is ill-formed because `y` is `const` and `s::g()` is a non-const member function, that is, `s::g()` is less-qualified than the object expression `y`. — *end example*]

**4** [*Note 3*: Constructors and destructors cannot be declared `const`, `volatile`, or `const` `volatile`. However, these functions can be invoked to create and destroy objects with cv-qualified types; see [[class.ctor]](class.ctor.md) and [[class.dtor]](class.dtor.md). — *end note*]
