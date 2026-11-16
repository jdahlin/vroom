# 11.4.1 Member functions [class.mfct]

[Contents](../README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4 Class members \[class.mem\]](class.mem.md) · next: [11.4.2 Non-static member functions \[class.mfct.non-static\]](class.mfct.non-static.md)

**1** A member function may be defined ([[dcl.fct.def]](../dcl/dcl.fct.def.md)) in its class definition, in which case it is an inline ([[dcl.inline]](../dcl/dcl.inline.md)) member function if it is attached to the global module, or it may be defined outside of its class definition if it has already been declared but not defined in its class definition. [*Note 1*: A member function is also inline if it is declared `inline`, `constexpr`, or `consteval`. — *end note*]

**2** A member function definition that appears outside of the class definition shall appear in a namespace scope enclosing the class definition. Except for member function definitions that appear outside of a class definition, and except for explicit specializations of member functions of class templates and member function templates ([[temp.spec]](../temp/temp.spec.md)) appearing outside of the class definition, a member function shall not be redeclared. [*Note 2*: There can be at most one definition of a non-inline member function in a program. There may be more than one inline member function definition in a program. See [[basic.def.odr]](../basic/basic.def.odr.md) and [[dcl.inline]](../dcl/dcl.inline.md). — *end note*] [*Note 3*: Member functions of a class have the linkage of the name of the class. See [[basic.link]](../basic/basic.link.md). — *end note*]

**3** If the definition of a member function is lexically outside its class definition, the member function name shall be qualified by its class name using the `::` operator. [*Note 4*: A name used in a member function definition (that is, in the *parameter-declaration-clause* including the default arguments ([[dcl.fct.default]](../dcl/dcl.fct.default.md)) or in the member function body) is looked up as described in [[basic.lookup]](../basic/basic.lookup.md). — *end note*] [*Example 1*:

```cpp
struct X {
  typedef int T;
  static T count;
  void f(T);
};
void X::f(T t = count) { }
```

The member function `f` of class `X` is defined in global scope; the notation `X::f` specifies that the function `f` is a member of class `X` and in the scope of class `X`. In the function definition, the parameter type `T` refers to the typedef member `T` declared in class `X` and the default argument `count` refers to the static data member `count` declared in class `X`. — *end example*]

**4** [*Note 5*: A `static` local variable or local type in a member function always refers to the same entity, whether or not the member function is inline. — *end note*]

**5** Previously declared member functions may be mentioned in friend declarations.

**6** Member functions of a local class shall be defined inline in their class definition, if they are defined at all.

**7** [*Note 6*: A member function can be declared (but not defined) using a typedef for a function type. The resulting member function has exactly the same type as it would have if the function declarator were provided explicitly, see [[dcl.fct]](../dcl/dcl.fct.md). For example,

```cpp
typedef void fv();
typedef void fvc() const;
struct S {
  fv memfunc1;      // equivalent to: void memfunc1();
  void memfunc2();
  fvc memfunc3;     // equivalent to: void memfunc3() const;
};
fv  S::* pmfv1 = &S::memfunc1;
fv  S::* pmfv2 = &S::memfunc2;
fvc S::* pmfv3 = &S::memfunc3;
```

Also see [[temp.arg]](../temp/temp.arg.md). — *end note*]
