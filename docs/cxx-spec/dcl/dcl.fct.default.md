# 9.3.3.6 Default arguments [dcl.fct.default]

[Contents](../README.md) · up: [9.3.3 Meaning of declarators \[dcl.meaning\]](dcl.meaning.md) · prev: [9.3.3.5 Functions \[dcl.fct\]](dcl.fct.md) · next: [9.4 Initializers \[dcl.init\]](dcl.init.md)

**1** If an *initializer-clause* is specified in a *parameter-declaration* this *initializer-clause* is used as a default argument. [*Note 1*: Default arguments will be used in calls where trailing arguments are missing ([[expr.call]](../expr/expr.call.md)). — *end note*]

**2** [*Example 1*: The declaration

```cpp
void point(int = 3, int = 4);
```

declares a function that can be called with zero, one, or two arguments of type `int`. It can be called in any of these ways:

```cpp
point(1,2);  point(1);  point();
```

The last two calls are equivalent to `point(1,4)` and `point(3,4)`, respectively. — *end example*]

**3** A default argument shall be specified only in the *parameter-declaration-clause* of a function declaration or *lambda-declarator* or in a *template-parameter* ([[temp.param]](../temp/temp.param.md)); in the latter case, the *initializer-clause* shall be an *assignment-expression*. A default argument shall not be specified for a template parameter pack or a function parameter pack. If it is specified in a *parameter-declaration-clause*, it shall not occur within a *declarator* or *abstract-declarator* of a *parameter-declaration*.[^1]

**4** For non-template functions, default arguments can be added in later declarations of a function in the same scope. Declarations in different scopes have completely distinct sets of default arguments. That is, declarations in inner scopes do not acquire default arguments from declarations in outer scopes, and vice versa. In a given function declaration, each parameter subsequent to a parameter with a default argument shall have a default argument supplied in this or a previous declaration, unless the parameter was expanded from a parameter pack, or shall be a function parameter pack. [*Note 2*: A default argument cannot be redefined by a later declaration (not even to the same value) ([[basic.def.odr]](../basic/basic.def.odr.md)). — *end note*] [*Example 2*:

```cpp
void g(int = 0, ...);           // OK, ellipsis is not a parameter so it can follow
                                // a parameter with a default argument
void f(int, int);
void f(int, int = 7);
void h() {
  f(3);                         // OK, calls f(3, 7)
  void f(int = 1, int);         // error: does not use default from surrounding scope
}
void m() {
  void f(int, int);             // has no defaults
  f(4);                         // error: wrong number of arguments
  void f(int, int = 5);         // OK
  f(4);                         // OK, calls f(4, 5);
  void f(int, int = 5);         // error: cannot redefine, even to same value
}
void n() {
  f(6);                         // OK, calls f(6, 7)
}
template<class ... T> struct C {
  void f(int n = 0, T...);
};
C<int> c;                       // OK, instantiates declaration void C::f(int n = 0, int)
```

— *end example*] For a given inline function defined in different translation units, the accumulated sets of default arguments at the end of the translation units shall be the same; no diagnostic is required. If a friend declaration specifies a default argument expression, that declaration shall be a definition and shall be the only declaration of the function or function template in the translation unit.

**5** The default argument has the same semantic constraints as the initializer in a declaration of a variable of the parameter type, using the copy-initialization semantics ([[dcl.init]](dcl.init.md)). The names in the default argument are bound, and the semantic constraints are checked, at the point where the default argument appears. Name lookup and checking of semantic constraints for default arguments in function templates and in member functions of class templates are performed as described in [[temp.inst]](../temp/temp.inst.md). [*Example 3*: In the following code, `g` will be called with the value `f(2)`:

```cpp
int a = 1;
int f(int);
int g(int x = f(a));            // default argument: f(::a)

void h() {
  a = 2;
  {
  int a = 3;
  g();                          // g(f(::a))
  }
}
```

— *end example*] [*Note 3*: In member function declarations, names in default arguments are looked up as described in [[basic.lookup.unqual]](../basic/basic.lookup.unqual.md). Access checking applies to names in default arguments as described in [[class.access]](../class/class.access.md). — *end note*]

**6** Except for member functions of class templates, the default arguments in a member function definition that appears outside of the class definition are added to the set of default arguments provided by the member function declaration in the class definition; the program is ill-formed if a default constructor ([[class.default.ctor]](../class/class.default.ctor.md)), copy or move constructor ([[class.copy.ctor]](../class/class.copy.ctor.md)), or copy or move assignment operator ([[class.copy.assign]](../class/class.copy.assign.md)) is so declared. Default arguments for a member function of a class template shall be specified on the initial declaration of the member function within the class template. [*Example 4*:

```cpp
class C {
  void f(int i = 3);
  void g(int i, int j = 99);
};

void C::f(int i = 3) {}         // error: default argument already specified in class scope
void C::g(int i = 88, int j) {} // in this translation unit, C::g can be called with no argument
```

— *end example*]

**7** [*Note 4*: A local variable cannot be odr-used ([[basic.def.odr]](../basic/basic.def.odr.md)) in a default argument. — *end note*] [*Example 5*:

```cpp
void f() {
  int i;
  extern void g(int x = i);         // error
  extern void h(int x = sizeof(i)); // OK
  // ...
}
```

— *end example*]

**8** [*Note 5*: The keyword `this` may not appear in a default argument of a member function; see [[expr.prim.this]](../expr/expr.prim.this.md). [*Example 6*:

```cpp
class A {
  void f(A* p = this) { }           // error
};
```

— *end example*] — *end note*]

**9** A default argument is evaluated each time the function is called with no argument for the corresponding parameter. A parameter shall not appear as a potentially-evaluated expression in a default argument. Parameters of a function declared before a default argument are in scope and can hide namespace and class member names. [*Example 7*:

```cpp
int a;
int f(int a, int b = a);            // error: parameter a used as default argument
typedef int I;
int g(float I, int b = I(2));       // error: parameter I found
int h(int a, int b = sizeof(a));    // OK, unevaluated operand
```

— *end example*] A non-static member shall not appear in a default argument unless it appears as the *id-expression* of a class member access expression ([[expr.ref]](../expr/expr.ref.md)) or unless it is used to form a pointer to member ([[expr.unary.op]](../expr/expr.unary.op.md)). [*Example 8*: The declaration of `X::mem1()` in the following example is ill-formed because no object is supplied for the non-static member `X::a` used as an initializer.

```cpp
int b;
class X {
  int a;
  int mem1(int i = a);              // error: non-static member a used as default argument
  int mem2(int i = b);              // OK; use X::b
  static int b;
};
```

The declaration of `X::mem2()` is meaningful, however, since no object is needed to access the static member `X::b`. Classes, objects, and members are described in [[class]](../class/class.md). — *end example*] A default argument is not part of the type of a function. [*Example 9*:

```cpp
int f(int = 0);

void h() {
  int j = f(1);
  int k = f();                      // OK, means f(0)
}

int (*p1)(int) = &f;
int (*p2)() = &f;                   // error: type mismatch
```

— *end example*] When a declaration of a function is introduced by way of a *using-declaration* ([[namespace.udecl]](namespace.udecl.md)), any default argument information associated with the declaration is made known as well. If the function is redeclared thereafter in the namespace with additional default arguments, the additional arguments are also known at any point following the redeclaration where the *using-declaration* is in scope.

**10** A virtual function call ([[class.virtual]](../class/class.virtual.md)) uses the default arguments in the declaration of the virtual function determined by the static type of the pointer or reference denoting the object. An overriding function in a derived class does not acquire default arguments from the function it overrides. [*Example 10*:

```cpp
struct A {
  virtual void f(int a = 7);
};
struct B : public A {
  void f(int a);
};
void m() {
  B* pb = new B;
  A* pa = pb;
  pa->f();          // OK, calls pa->B::f(7)
  pb->f();          // error: wrong number of arguments for B::f()
}
```

— *end example*]

[^1]: This means that default arguments cannot appear, for example, in declarations of pointers to functions, references to functions, or `typedef` declarations.
