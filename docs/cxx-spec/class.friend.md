# 11.9.3 Friends [class.friend]

[Contents](README.md) · up: [11.9 Member access control \[class.access\]](class.access.md) · prev: [11.9.2 Accessibility of base classes and base class members \[class.access.base\]](class.access.base.md) · next: [11.9.4 Protected member access \[class.protected\]](class.protected.md)

**1** A friend of a class is a function or class that is given permission to use the private and protected member names from the class. A class specifies its friends, if any, by way of friend declarations. Such declarations give special access rights to the friends, but they do not make the nominated friends members of the befriending class. [*Example 1*: The following example illustrates the differences between members and friends:

```cpp
class X {
  int a;
  friend void friend_set(X*, int);
public:
  void member_set(int);
};

void friend_set(X* p, int i) { p->a = i; }
void X::member_set(int i) { a = i; }

void f() {
  X obj;
  friend_set(&obj,10);
  obj.member_set(10);
}
```

— *end example*]

**2** Declaring a class to be a friend implies that the names of private and protected members from the class granting friendship can be accessed in the *base-specifier*s and member declarations of the befriended class. [*Example 2*:

```cpp
class A {
  class B { };
  friend class X;
};

struct X : A::B {               // OK: A::B accessible to friend
  A::B mx;                      // OK: A::B accessible to member of friend
  class Y {
    A::B my;                    // OK: A::B accessible to nested member of friend
  };
};
```

— *end example*] [*Example 3*:

```cpp
class X {
  enum { a=100 };
  friend class Y;
};

class Y {
  int v[X::a];                  // OK, Y is a friend of X
};

class Z {
  int v[X::a];                  // error: X::a is private
};
```

— *end example*]

A class shall not be defined in a friend declaration. [*Example 4*:

```cpp
class A {
  friend class B { };           // error: cannot define class in friend declaration
};
```

— *end example*]

**3** A friend declaration that does not declare a function shall have one of the following forms:

```bnf
'friend' elaborated-type-specifier ';'
'friend' simple-type-specifier ';'
'friend' typename-specifier ';'
```

[*Note 1*: A friend declaration may be the *declaration* in a *template-declaration* ([[temp.pre]](temp.pre.md), [[temp.friend]](temp.friend.md)). — *end note*] If the type specifier in a `friend` declaration designates a (possibly cv-qualified) class type, that class is declared as a friend; otherwise, the friend declaration is ignored. [*Example 5*:

```cpp
class C;
typedef C Ct;

class X1 {
  friend C;                     // OK: class C is a friend
};

class X2 {
  friend Ct;                    // OK: class C is a friend
  friend D;                     // error: no type-name D in scope
  friend class D;               // OK: elaborated-type-specifier declares new class
};

template <typename T> class R {
  friend T;
};

R<C> rc;                        // class C is a friend of R<C>
R<int> Ri;                      // OK: "friend int;" is ignored
```

— *end example*]

**4** A function first declared in a friend declaration has the linkage of the namespace of which it is a member ([[basic.link]](basic.link.md), [[namespace.memdef]](namespace.memdef.md)). Otherwise, the function retains its previous linkage ([[dcl.stc]](dcl.stc.md)).

**5** When a friend declaration refers to an overloaded name or operator, only the function specified by the parameter types becomes a friend. A member function of a class `X` can be a friend of a class `Y`. [*Example 6*:

```cpp
class Y {
  friend char* X::foo(int);
  friend X::X(char);            // constructors can be friends
  friend X::~X();               // destructors can be friends
};
```

— *end example*]

**6** A function can be defined in a friend declaration of a class if and only if the class is a non-local class ([[class.local]](class.local.md)), the function name is unqualified, and the function has namespace scope. [*Example 7*:

```cpp
class M {
  friend void f() { }           // definition of global f, a friend of M,
                                // not the definition of a member function
};
```

— *end example*]

**7** Such a function is implicitly an inline ([[dcl.inline]](dcl.inline.md)) function if it is attached to the global module. A friend function defined in a class is in the (lexical) scope of the class in which it is defined. A friend function defined outside the class is not ([[basic.lookup.unqual]](basic.lookup.unqual.md)).

**8** No *storage-class-specifier* shall appear in the *decl-specifier-seq* of a friend declaration.

**9** A name nominated by a friend declaration shall be accessible in the scope of the class containing the friend declaration. The meaning of the friend declaration is the same whether the friend declaration appears in the private, protected, or public ([[class.mem]](class.mem.md)) portion of the class *member-specification*.

**10** Friendship is neither inherited nor transitive. [*Example 8*:

```cpp
class A {
  friend class B;
  int a;
};

class B {
  friend class C;
};

class C  {
  void f(A* p) {
    p->a++;         // error: C is not a friend of A despite being a friend of a friend
  }
};

class D : public B  {
  void f(A* p) {
    p->a++;         // error: D is not a friend of A despite being derived from a friend
  }
};
```

— *end example*]

**11** If a friend declaration appears in a local class ([[class.local]](class.local.md)) and the name specified is an unqualified name, a prior declaration is looked up without considering scopes that are outside the innermost enclosing non-class scope. For a friend function declaration, if there is no prior declaration, the program is ill-formed. For a friend class declaration, if there is no prior declaration, the class that is specified belongs to the innermost enclosing non-class scope, but if it is subsequently referenced, its name is not found by name lookup until a matching declaration is provided in the innermost enclosing non-class scope. [*Example 9*:

```cpp
class X;
void a();
void f() {
  class Y;
  extern void b();
  class A {
  friend class X;   // OK, but X is a local class, not ::X
  friend class Y;   // OK
  friend class Z;   // OK, introduces local class Z
  friend void a();  // error, ::a is not considered
  friend void b();  // OK
  friend void c();  // error
  };
  X* px;            // OK, but ::X is found
  Z* pz;            // error: no Z is found
}
```

— *end example*]
