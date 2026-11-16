# 6.2 Declarations and definitions [basic.def]

[Contents](README.md) · up: [6 Basics \[basic\]](basic.md) · prev: [6.1 Preamble \[basic.pre\]](basic.pre.md) · next: [6.3 One-definition rule \[basic.def.odr\]](basic.def.odr.md)

**1** A declaration ([[dcl.dcl]](dcl.dcl.md)) may introduce one or more names into a translation unit or redeclare names introduced by previous declarations. If so, the declaration specifies the interpretation and semantic properties of these names. A declaration may also have effects including:

- a static assertion ([[dcl.pre]](dcl.pre.md)),
- controlling template instantiation ([[temp.explicit]](temp.explicit.md)),
- guiding template argument deduction for constructors ([[temp.deduct.guide]](temp.deduct.guide.md)),
- use of attributes ([[dcl.attr]](dcl.attr.md)), and
- nothing (in the case of an *empty-declaration*).

**2** Each entity declared by a *declaration* is also *defined* by that declaration unless:

- it declares a function without specifying the function’s body ([[dcl.fct.def]](dcl.fct.def.md)),
- it contains the `extern` specifier ([[dcl.stc]](dcl.stc.md)) or a *linkage-specification*[^1] ([[dcl.link]](dcl.link.md)) and neither an *initializer* nor a *function-body*,
- it declares a non-inline static data member in a class definition ([[class.mem]](class.mem.md), [[class.static]](class.static.md)),
- it declares a static data member outside a class definition and the variable was defined within the class with the `constexpr` specifier (this usage is deprecated; see [[depr.static.constexpr]](depr.static.constexpr.md)),
- it is introduced by an *elaborated-type-specifier* ([[class.name]](class.name.md)),
- it is an *opaque-enum-declaration* ([[dcl.enum]](dcl.enum.md)),
- it is a *template-parameter* ([[temp.param]](temp.param.md)),
- it is a *parameter-declaration* ([[dcl.fct]](dcl.fct.md)) in a function declarator that is not the *declarator* of a *function-definition*,
- it is a `typedef` declaration ([[dcl.typedef]](dcl.typedef.md)),
- it is an *alias-declaration* ([[dcl.typedef]](dcl.typedef.md)),
- it is a *using-declaration* ([[namespace.udecl]](namespace.udecl.md)),
- it is a *deduction-guide* ([[temp.deduct.guide]](temp.deduct.guide.md)),
- it is a *static\_assert-declaration* ([[dcl.pre]](dcl.pre.md)),
- it is an *attribute-declaration* ([[dcl.pre]](dcl.pre.md)),
- it is an *empty-declaration* ([[dcl.pre]](dcl.pre.md)),
- it is a *using-directive* ([[namespace.udir]](namespace.udir.md)),
- it is a *using-enum-declaration* ([[enum.udecl]](enum.udecl.md)),
- it is a *template-declaration* ([[temp.pre]](temp.pre.md)) whose *template-head* is not followed by either a *concept-definition* or a *declaration* that defines a function, a class, a variable, or a static data member.
- it is an explicit instantiation declaration ([[temp.explicit]](temp.explicit.md)), or
- it is an explicit specialization ([[temp.expl.spec]](temp.expl.spec.md)) whose *declaration* is not a definition.

A declaration is said to be a *definition* of each entity that it defines. [*Example 1*: All but one of the following are definitions:

```cpp
int a;                          // defines a
extern const int c = 1;         // defines c
int f(int x) { return x+a; }    // defines f and defines x
struct S { int a; int b; };     // defines S, S::a, and S::b
struct X {                      // defines X
  int x;                        // defines non-static data member x
  static int y;                 // declares static data member y
  X(): x(0) { }                 // defines a constructor of X
};
int X::y = 1;                   // defines X::y
enum { up, down };              // defines up and down
namespace N { int d; }          // defines N and N::d
namespace N1 = N;               // defines N1
X anX;                          // defines anX
```

whereas these are just declarations:

```cpp
extern int a;                   // declares a
extern const int c;             // declares c
int f(int);                     // declares f
struct S;                       // declares S
typedef int Int;                // declares Int
extern X anotherX;              // declares anotherX
using N::d;                     // declares d
```

— *end example*]

**3** [*Note 1*: In some circumstances, C++ implementations implicitly define the default constructor ([[class.default.ctor]](class.default.ctor.md)), copy constructor, move constructor ([[class.copy.ctor]](class.copy.ctor.md)), copy assignment operator, move assignment operator ([[class.copy.assign]](class.copy.assign.md)), or destructor ([[class.dtor]](class.dtor.md)) member functions. — *end note*] [*Example 2*: Given

```cpp
#include <string>

struct C {
  std::string s;                // std::string is the standard library class ([string.classes])
};

int main() {
  C a;
  C b = a;
  b = a;
}
```

the implementation will implicitly define functions to make the definition of `C` equivalent to

```cpp
struct C {
  std::string s;
  C() : s() { }
  C(const C& x): s(x.s) { }
  C(C&& x): s(static_cast<std::string&&>(x.s)) { }
      //    : s(std::move(x.s))
  C& operator=(const C& x) { s = x.s; return *this; }
  C& operator=(C&& x) { s = static_cast<std::string&&>(x.s); return *this; }
      //                s = std::move(x.s); return *this;
  ~C() { }
};
```

— *end example*]

**4** [*Note 2*: A class name can also be implicitly declared by an *elaborated-type-specifier* ([[dcl.type.elab]](dcl.type.elab.md)). — *end note*]

**5** In the definition of an object, the type of that object shall not be an incomplete type ([[basic.types]](basic.types.md)), an abstract class type ([[class.abstract]](class.abstract.md)), or a (possibly multi-dimensional) array thereof.

[^1]: Appearing inside the brace-enclosed *declaration-seq* in a *linkage-specification* does not affect whether a declaration is a definition.
