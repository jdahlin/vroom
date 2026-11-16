# 6.5.1 Unqualified name lookup [basic.lookup.unqual]

[Contents](README.md) · up: [6.5 Name lookup \[basic.lookup\]](basic.lookup.md) · prev: [6.5 Name lookup \[basic.lookup\]](basic.lookup.md) · next: [6.5.2 Argument-dependent name lookup \[basic.lookup.argdep\]](basic.lookup.argdep.md)

**1** In all the cases listed in [[basic.lookup.unqual]](basic.lookup.unqual.md), the scopes are searched for a declaration in the order listed in each of the respective categories; name lookup ends as soon as a declaration is found for the name. If no declaration is found, the program is ill-formed.

**2** The declarations from the namespace nominated by a *using-directive* become visible in a namespace enclosing the *using-directive*; see [[namespace.udir]](namespace.udir.md). For the purpose of the unqualified name lookup rules described in [[basic.lookup.unqual]](basic.lookup.unqual.md), the declarations from the namespace nominated by the *using-directive* are considered members of that enclosing namespace.

**3** The lookup for an unqualified name used as the *postfix-expression* of a function call is described in [[basic.lookup.argdep]](basic.lookup.argdep.md). [*Note 1*: For purposes of determining (during parsing) whether an expression is a *postfix-expression* for a function call, the usual name lookup rules apply. In some cases a name followed by `<` is treated as a *template-name* even though name lookup did not find a *template-name* (see [[temp.names]](temp.names.md)). For example,

```cpp
int h;
void g();
namespace N {
  struct A {};
  template <class T> int f(T);
  template <class T> int g(T);
  template <class T> int h(T);
}

int x = f<N::A>(N::A());        // OK: lookup of f finds nothing, f treated as template name
int y = g<N::A>(N::A());        // OK: lookup of g finds a function, g treated as template name
int z = h<N::A>(N::A());        // error: h< does not begin a template-id
```

The rules in [[basic.lookup.argdep]](basic.lookup.argdep.md) have no effect on the syntactic interpretation of an expression. For example,

```cpp
typedef int f;
namespace N {
  struct A {
    friend void f(A &);
    operator int();
    void g(A a) {
      int i = f(a);             // f is the typedef, not the friend function: equivalent to int(a)
    }
  };
}
```

Because the expression is not a function call, the argument-dependent name lookup ([[basic.lookup.argdep]](basic.lookup.argdep.md)) does not apply and the friend function `f` is not found. — *end note*]

**4** A name used in global scope, outside of any function, class or user-declared namespace, shall be declared before its use in global scope.

**5** A name used in a user-declared namespace outside of the definition of any function or class shall be declared before its use in that namespace or before its use in a namespace enclosing its namespace.

**6** In the definition of a function that is a member of namespace `N`, a name used after the function’s *declarator-id*[^1] shall be declared before its use in the block in which it is used or in one of its enclosing blocks ([[stmt.block]](stmt.block.md)) or shall be declared before its use in namespace `N` or, if `N` is a nested namespace, shall be declared before its use in one of `N`’s enclosing namespaces. [*Example 1*:

```cpp
namespace A {
  namespace N {
    void f();
  }
}
void A::N::f() {
  i = 5;
  // The following scopes are searched for a declaration of i:
  // 1) outermost block scope of A::N::f, before the use of i
  // 2) scope of namespace N
  // 3) scope of namespace A
  // 4) global scope, before the definition of A::N::f
}
```

— *end example*]

**7** A name used in the definition of a class `X`[^2] outside of a complete-class context ([[class.mem]](class.mem.md)) of `X` shall be declared in one of the following ways:

- before its use in class `X` or be a member of a base class of `X` ([[class.member.lookup]](class.member.lookup.md)), or
- if `X` is a nested class of class `Y` ([[class.nest]](class.nest.md)), before the definition of `X` in `Y`, or shall be a member of a base class of `Y` (this lookup applies in turn to `Y`’s enclosing classes, starting with the innermost enclosing class),[^3] or
- if `X` is a local class ([[class.local]](class.local.md)) or is a nested class of a local class, before the definition of class `X` in a block enclosing the definition of class `X`, or
- if `X` is a member of namespace `N`, or is a nested class of a class that is a member of `N`, or is a local class or a nested class within a local class of a function that is a member of `N`, before the definition of class `X` in namespace `N` or in one of `N`’s enclosing namespaces.

[*Example 2*:

```cpp
namespace M {
  class B { };
}
```

```cpp
namespace N {
  class Y : public M::B {
    class X {
      int a[i];
    };
  };
}

// The following scopes are searched for a declaration of i:
// 1) scope of class N::Y::X, before the use of i
// 2) scope of class N::Y, before the definition of N::Y::X
// 3) scope of N::Y's base class M::B
// 4) scope of namespace N, before the definition of N::Y
// 5) global scope, before the definition of N
```

— *end example*] [*Note 2*: When looking for a prior declaration of a class or function introduced by a friend declaration, scopes outside of the innermost enclosing namespace scope are not considered; see [[namespace.memdef]](namespace.memdef.md). — *end note*] [*Note 3*: [[basic.scope.class]](basic.scope.class.md) further describes the restrictions on the use of names in a class definition. [[class.nest]](class.nest.md) further describes the restrictions on the use of names in nested class definitions. [[class.local]](class.local.md) further describes the restrictions on the use of names in local class definitions. — *end note*]

**8** For the members of a class `X`, a name used in a complete-class context ([[class.mem]](class.mem.md)) of `X` or in the definition of a class member outside of the definition of `X`, following the member’s *declarator-id*[^4], shall be declared in one of the following ways:

- before its use in the block in which it is used or in an enclosing block ([[stmt.block]](stmt.block.md)), or
- shall be a member of class `X` or be a member of a base class of `X` ([[class.member.lookup]](class.member.lookup.md)), or
- if `X` is a nested class of class `Y` ([[class.nest]](class.nest.md)), shall be a member of `Y`, or shall be a member of a base class of `Y` (this lookup applies in turn to `Y`’s enclosing classes, starting with the innermost enclosing class),[^5] or
- if `X` is a local class ([[class.local]](class.local.md)) or is a nested class of a local class, before the definition of class `X` in a block enclosing the definition of class `X`, or
- if `X` is a member of namespace `N`, or is a nested class of a class that is a member of `N`, or is a local class or a nested class within a local class of a function that is a member of `N`, before the use of the name, in namespace `N` or in one of `N`’s enclosing namespaces.

[*Example 3*:

```cpp
class B { };
namespace M {
  namespace N {
    class X : public B {
      void f();
    };
  }
}
void M::N::X::f() {
  i = 16;
}

// The following scopes are searched for a declaration of i:
// 1) outermost block scope of M::N::X::f, before the use of i
// 2) scope of class M::N::X
// 3) scope of M::N::X's base class B
// 4) scope of namespace M::N
// 5) scope of namespace M
// 6) global scope, before the definition of M::N::X::f
```

— *end example*] [*Note 4*: [[class.mfct]](class.mfct.md) and [[class.static]](class.static.md) further describe the restrictions on the use of names in member function definitions. [[class.nest]](class.nest.md) further describes the restrictions on the use of names in the scope of nested classes. [[class.local]](class.local.md) further describes the restrictions on the use of names in local class definitions. — *end note*]

**9** Name lookup for a name used in the definition of a friend function ([[class.friend]](class.friend.md)) defined inline in the class granting friendship shall proceed as described for lookup in member function definitions. If the friend function is not defined in the class granting friendship, name lookup in the friend function definition shall proceed as described for lookup in namespace member function definitions.

**10** In a friend declaration naming a member function, a name used in the function declarator and not part of a *template-argument* in the *declarator-id* is first looked up in the scope of the member function’s class ([[class.member.lookup]](class.member.lookup.md)). If it is not found, or if the name is part of a *template-argument* in the *declarator-id*, the look up is as described for unqualified names in the definition of the class granting friendship. [*Example 4*:

```cpp
struct A {
  typedef int AT;
  void f1(AT);
  void f2(float);
  template <class T> void f3();
};
struct B {
  typedef char AT;
  typedef float BT;
  friend void A::f1(AT);        // parameter type is A::AT
  friend void A::f2(BT);        // parameter type is B::BT
  friend void A::f3<AT>();      // template argument is B::AT
};
```

— *end example*]

**11** During the lookup for a name used as a default argument ([[dcl.fct.default]](dcl.fct.default.md)) in a function *parameter-declaration-clause* or used in the *expression* of a *mem-initializer* for a constructor ([[class.base.init]](class.base.init.md)), the function parameter names are visible and hide the names of entities declared in the block, class or namespace scopes containing the function declaration. [*Note 5*: [[dcl.fct.default]](dcl.fct.default.md) further describes the restrictions on the use of names in default arguments. [[class.base.init]](class.base.init.md) further describes the restrictions on the use of names in a *ctor-initializer*. — *end note*]

**12** During the lookup of a name used in the *constant-expression* of an *enumerator-definition*, previously declared *enumerator*s of the enumeration are visible and hide the names of entities declared in the block, class, or namespace scopes containing the *enum-specifier*.

**13** A name used in the definition of a `static` data member of class `X` ([[class.static.data]](class.static.data.md)) (after the *qualified-id* of the static member) is looked up as if the name was used in a member function of `X`. [*Note 6*: [[class.static.data]](class.static.data.md) further describes the restrictions on the use of names in the definition of a `static` data member. — *end note*]

**14** If a variable member of a namespace is defined outside of the scope of its namespace then any name that appears in the definition of the member (after the *declarator-id*) is looked up as if the definition of the member occurred in its namespace. [*Example 5*:

```cpp
namespace N {
  int i = 4;
  extern int j;
}

int i = 2;

int N::j = i;       // N::j == 4
```

— *end example*]

**15** A name used in the handler for a *function-try-block* ([[except.pre]](except.pre.md)) is looked up as if the name was used in the outermost block of the function definition. In particular, the function parameter names shall not be redeclared in the *exception-declaration* nor in the outermost block of a handler for the *function-try-block*. Names declared in the outermost block of the function definition are not found when looked up in the scope of a handler for the *function-try-block*. [*Note 7*: But function parameter names are found. — *end note*]

**16** [*Note 8*: The rules for name lookup in template definitions are described in [[temp.res]](temp.res.md). — *end note*]

[^1]: This refers to unqualified names that occur, for instance, in a type or default argument in the *parameter-declaration-clause* or used in the function body.
[^2]: This refers to unqualified names following the class name; such a name may be used in a *base-specifier* or in the *member-specification* of the class definition.
[^3]: This lookup applies whether the definition of `X` is nested within `Y`’s definition or whether `X`’s definition appears in a namespace scope enclosing `Y`’s definition ([[class.nest]](class.nest.md)).
[^4]: That is, an unqualified name that occurs, for instance, in a type in the *parameter-declaration-clause* or in the *noexcept-specifier*.
[^5]: This lookup applies whether the member function is defined within the definition of class `X` or whether the member function is defined in a namespace scope enclosing `X`’s definition.
