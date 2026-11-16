# C.1.5 [[class]](../class/class.md): classes [diff.cpp17.class]

[Contents](../README.md) · up: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · prev: [C.1.4 [[dcl.dcl]](../dcl/dcl.dcl.md): declarations \[diff.cpp17.dcl.dcl\]](diff.cpp17.dcl.dcl.md) · next: [C.1.6 [[over]](../over/over.md): overloading \[diff.cpp17.over\]](diff.cpp17.over.md)

**1** **Affected subclauses:** [[class.ctor]](../class/class.ctor.md), [[class.conv.fct]](../class/class.conv.fct.md) <br>**Change:** The class name can no longer be used parenthesized immediately after an `explicit` *decl-specifier* in a constructor declaration. The *conversion-function-id* can no longer be used parenthesized immediately after an `explicit` *decl-specifier* in a conversion function declaration. <br>**Rationale:** Necessary for new functionality. <br>**Effect on original feature:** Valid C++ 2017 code may fail to compile in this International Standard. For example:

```cpp
struct S {
  explicit (S)(const S&);       // ill-formed; previously well-formed
  explicit (operator int)();    // ill-formed; previously well-formed
  explicit(true) (S)(int);      // OK
};
```

**2** **Affected subclauses:** [[class.ctor]](../class/class.ctor.md), [[class.dtor]](../class/class.dtor.md) <br>**Change:** A *simple-template-id* is no longer valid as the *declarator-id* of a constructor or destructor. <br>**Rationale:** Remove potentially error-prone option for redundancy. <br>**Effect on original feature:** Valid C++ 2017 code may fail to compile in this International Standard. For example:

```cpp
template<class T>
struct A {
  A<T>();           // error: simple-template-id not allowed for constructor
  A(int);           // OK, injected-class-name used
  ~A<T>();          // error: simple-template-id not allowed for destructor
};
```

**3** **Affected subclause:** [[class.copy.elision]](../class/class.copy.elision.md) <br>**Change:** A function returning an implicitly movable entity may invoke a constructor taking an rvalue reference to a type different from that of the returned expression. Function and catch-clause parameters can be thrown using move constructors. <br>**Rationale:** Side effect of making it easier to write more efficient code that takes advantage of moves. <br>**Effect on original feature:** Valid C++ 2017 code may fail to compile or have different semantics in this International Standard. For example:

```cpp
struct base {
  base();
  base(base const &);
private:
  base(base &&);
};

struct derived : base {};

base f(base b) {
  throw b;                      // error: base(base &&) is private
  derived d;
  return d;                     // error: base(base &&) is private
}

struct S {
  S(const char *s) : m(s) { }
  S(const S&) = default;
  S(S&& other) : m(other.m) { other.m = nullptr; }
  const char * m;
};

S consume(S&& s) { return s; }

void g() {
  S s("text");
  consume(static_cast<S&&>(s));
  char c = *s.m;                // undefined behavior; previously ok
}
```
