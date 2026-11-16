# 11.7.3 Abstract classes [class.abstract]

[Contents](README.md) · up: [11.7 Derived classes \[class.derived\]](class.derived.md) · prev: [11.7.2 Virtual functions \[class.virtual\]](class.virtual.md) · next: [11.8 Member name lookup \[class.member.lookup\]](class.member.lookup.md)

**1** [*Note 1*: The abstract class mechanism supports the notion of a general concept, such as a `shape`, of which only more concrete variants, such as `circle` and `square`, can actually be used. An abstract class can also be used to define an interface for which derived classes provide a variety of implementations. — *end note*]

**2** A virtual function is specified as a *pure virtual function* by using a *pure-specifier* ([[class.mem]](class.mem.md)) in the function declaration in the class definition. [*Note 2*: Such a function might be inherited: see below. — *end note*] A class is an *abstract class* if it has at least one pure virtual function. [*Note 3*: An abstract class can be used only as a base class of some other class; no objects of an abstract class can be created except as subobjects of a class derived from it ([[basic.def]](basic.def.md), [[class.mem]](class.mem.md)). — *end note*] A pure virtual function need be defined only if called with, or as if with ([[class.dtor]](class.dtor.md)), the *qualified-id* syntax ([[expr.prim.id.qual]](expr.prim.id.qual.md)). [*Example 1*:

```cpp
class point { /* ... */ };
class shape {                   // abstract class
  point center;
public:
  point where() { return center; }
  void move(point p) { center=p; draw(); }
  virtual void rotate(int) = 0; // pure virtual
  virtual void draw() = 0;      // pure virtual
};
```

— *end example*] [*Note 4*: A function declaration cannot provide both a *pure-specifier* and a definition. — *end note*] [*Example 2*:

```cpp
struct C {
  virtual void f() = 0 { };     // error
};
```

— *end example*]

**3** [*Note 5*: An abstract class type cannot be used as a parameter or return type of a function being defined ([[dcl.fct]](dcl.fct.md)) or called ([[expr.call]](expr.call.md)), except as specified in [[dcl.type.simple]](dcl.type.simple.md). Further, an abstract class type cannot be used as the type of an explicit type conversion ([[expr.static.cast]](expr.static.cast.md), [[expr.reinterpret.cast]](expr.reinterpret.cast.md), [[expr.const.cast]](expr.const.cast.md)), because the resulting prvalue would be of abstract class type ([[basic.lval]](basic.lval.md)). However, pointers and references to abstract class types can appear in such contexts. — *end note*]

**4** A class is abstract if it contains or inherits at least one pure virtual function for which the final overrider is pure virtual. [*Example 3*:

```cpp
class ab_circle : public shape {
  int radius;
public:
  void rotate(int) { }
  // ab_circle::draw() is a pure virtual
};
```

Since `shape::draw()` is a pure virtual function `ab_circle::draw()` is a pure virtual by default. The alternative declaration,

```cpp
class circle : public shape {
  int radius;
public:
  void rotate(int) { }
  void draw();                  // a definition is required somewhere
};
```

would make class `circle` non-abstract and a definition of `circle::draw()` must be provided. — *end example*]

**5** [*Note 6*: An abstract class can be derived from a class that is not abstract, and a pure virtual function may override a virtual function which is not pure. — *end note*]

**6** Member functions can be called from a constructor (or destructor) of an abstract class; the effect of making a virtual call ([[class.virtual]](class.virtual.md)) to a pure virtual function directly or indirectly for the object being created (or destroyed) from such a constructor (or destructor) is undefined.
