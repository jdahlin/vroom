# 11.4.4.2 Copy/move constructors [class.copy.ctor]

[Contents](../README.md) · up: [11.4.4 Constructors \[class.ctor\]](class.ctor.md) · prev: [11.4.4.1 Default constructors \[class.default.ctor\]](class.default.ctor.md) · next: [11.4.5 Copy/move assignment operator \[class.copy.assign\]](class.copy.assign.md)

**1** A non-template constructor for class `X` is a copy constructor if its first parameter is of type `X&`, `const X&`, `volatile X&` or `const volatile X&`, and either there are no other parameters or else all other parameters have default arguments ([[dcl.fct.default]](../dcl/dcl.fct.default.md)). [*Example 1*: `X::X(const X&)` and `X::X(X&,int=1)` are copy constructors.

```cpp
struct X {
  X(int);
  X(const X&, int = 1);
};
X a(1);             // calls X(int);
X b(a, 0);          // calls X(const X&, int);
X c = b;            // calls X(const X&, int);
```

— *end example*]

**2** A non-template constructor for class `X` is a move constructor if its first parameter is of type `X&&`, `const X&&`, `volatile X&&`, or `const volatile X&&`, and either there are no other parameters or else all other parameters have default arguments ([[dcl.fct.default]](../dcl/dcl.fct.default.md)). [*Example 2*: `Y::Y(Y&&)` is a move constructor.

```cpp
struct Y {
  Y(const Y&);
  Y(Y&&);
};
extern Y f(int);
Y d(f(1));          // calls Y(Y&&)
Y e = d;            // calls Y(const Y&)
```

— *end example*]

**3** [*Note 1*: All forms of copy/move constructor may be declared for a class. [*Example 3*:

```cpp
struct X {
  X(const X&);
  X(X&);            // OK
  X(X&&);
  X(const X&&);     // OK, but possibly not sensible
};
```

— *end example*] — *end note*]

**4** [*Note 2*: If a class `X` only has a copy constructor with a parameter of type `X&`, an initializer of type `const` `X` or `volatile` `X` cannot initialize an object of type *cv* `X`. [*Example 4*:

```cpp
struct X {
  X();              // default constructor
  X(X&);            // copy constructor with a non-const parameter
};
const X cx;
X x = cx;           // error: X::X(X&) cannot copy cx into x
```

— *end example*] — *end note*]

**5** A declaration of a constructor for a class `X` is ill-formed if its first parameter is of type *cv* `X` and either there are no other parameters or else all other parameters have default arguments. A member function template is never instantiated to produce such a constructor signature. [*Example 5*:

```cpp
struct S {
  template<typename T> S(T);
  S();
};

S g;

void h() {
  S a(g);           // does not instantiate the member template to produce S::S<S>(S);
                    // uses the implicitly declared copy constructor
}
```

— *end example*]

**6** If the class definition does not explicitly declare a copy constructor, a non-explicit one is declared *implicitly*. If the class definition declares a move constructor or move assignment operator, the implicitly declared copy constructor is defined as deleted; otherwise, it is defined as defaulted ([[dcl.fct.def]](../dcl/dcl.fct.def.md)). The latter case is deprecated if the class has a user-declared copy assignment operator or a user-declared destructor ([[depr.impldec]](../depr/depr.impldec.md)).

**7** The implicitly-declared copy constructor for a class `X` will have the form

```cpp
X::X(const X&)
```

if each potentially constructed subobject of a class type `M` (or array thereof) has a copy constructor whose first parameter is of type `const` `M&` or `const` `volatile` `M&`.[^1] Otherwise, the implicitly-declared copy constructor will have the form

```cpp
X::X(X&)
```

**8** If the definition of a class `X` does not explicitly declare a move constructor, a non-explicit one will be implicitly declared as defaulted if and only if

- `X` does not have a user-declared copy constructor,
- `X` does not have a user-declared copy assignment operator,
- `X` does not have a user-declared move assignment operator, and
- `X` does not have a user-declared destructor.

[*Note 3*: When the move constructor is not implicitly declared or explicitly supplied, expressions that otherwise would have invoked the move constructor may instead invoke a copy constructor. — *end note*]

**9** The implicitly-declared move constructor for class `X` will have the form

```cpp
X::X(X&&)
```

**10** An implicitly-declared copy/move constructor is an inline public member of its class. A defaulted copy/move constructor for a class `X` is defined as deleted ([[dcl.fct.def.delete]](../dcl/dcl.fct.def.delete.md)) if `X` has:

- a potentially constructed subobject type `M` (or array thereof) that cannot be copied/moved because overload resolution ([[over.match]](../over/over.match.md)), as applied to find `M`’s corresponding constructor, results in an ambiguity or a function that is deleted or inaccessible from the defaulted constructor,
- a variant member whose corresponding constructor as selected by overload resolution is non-trivial,
- any potentially constructed subobject of a type with a destructor that is deleted or inaccessible from the defaulted constructor, or,
- for the copy constructor, a non-static data member of rvalue reference type.

[*Note 4*: A defaulted move constructor that is defined as deleted is ignored by overload resolution ([[over.match]](../over/over.match.md), [[over.over]](../over/over.over.md)). Such a constructor would otherwise interfere with initialization from an rvalue which can use the copy constructor instead. — *end note*]

**11** A copy/move constructor for class `X` is trivial if it is not user-provided and if:

- class `X` has no virtual functions ([[class.virtual]](class.virtual.md)) and no virtual base classes ([[class.mi]](class.mi.md)), and
- the constructor selected to copy/move each direct base class subobject is trivial, and
- for each non-static data member of `X` that is of class type (or array thereof), the constructor selected to copy/move that member is trivial;

otherwise the copy/move constructor is *non-trivial*.

**12** A copy/move constructor that is defaulted and not defined as deleted is *implicitly defined* when it is odr-used ([[basic.def.odr]](../basic/basic.def.odr.md)), when it is needed for constant evaluation ([[expr.const]](../expr/expr.const.md)), or when it is explicitly defaulted after its first declaration. [*Note 5*: The copy/move constructor is implicitly defined even if the implementation elided its odr-use ([[basic.def.odr]](../basic/basic.def.odr.md), [[class.temporary]](../basic/class.temporary.md)). — *end note*] If the implicitly-defined constructor would satisfy the requirements of a constexpr constructor ([[dcl.constexpr]](../dcl/dcl.constexpr.md)), the implicitly-defined constructor is `constexpr`.

**13** Before the defaulted copy/move constructor for a class is implicitly defined, all non-user-provided copy/move constructors for its potentially constructed subobjects are implicitly defined. [*Note 6*: An implicitly-declared copy/move constructor has an implied exception specification ([[except.spec]](../except/except.spec.md)). — *end note*]

**14** The implicitly-defined copy/move constructor for a non-union class `X` performs a memberwise copy/move of its bases and members. [*Note 7*: Default member initializers of non-static data members are ignored. See also the example in [[class.base.init]](class.base.init.md). — *end note*] The order of initialization is the same as the order of initialization of bases and members in a user-defined constructor (see [[class.base.init]](class.base.init.md)). Let `x` be either the parameter of the constructor or, for the move constructor, an xvalue referring to the parameter. Each base or non-static data member is copied/moved in the manner appropriate to its type:

- if the member is an array, each element is direct-initialized with the corresponding subobject of `x`;
- if a member `m` has rvalue reference type `T&&`, it is direct-initialized with `static_cast<T&&>(x.m)`;
- otherwise, the base or member is direct-initialized with the corresponding base or member of `x`.

Virtual base class subobjects shall be initialized only once by the implicitly-defined copy/move constructor (see [[class.base.init]](class.base.init.md)).

**15** The implicitly-defined copy/move constructor for a union `X` copies the object representation ([[basic.types]](../basic/basic.types.md)) of `X`. For each object nested within ([[intro.object]](../basic/intro.object.md)) the object that is the source of the copy, a corresponding object *o* nested within the destination is identified (if the object is a subobject) or created (otherwise), and the lifetime of *o* begins before the copy is performed.

[^1]: This implies that the reference parameter of the implicitly-declared copy constructor cannot bind to a `volatile` lvalue; see [[diff.class]](../diff/diff.class.md).
