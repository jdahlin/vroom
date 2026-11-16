# 11.4.2 Non-static member functions [class.mfct.non-static]

[Contents](../README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.1 Member functions \[class.mfct\]](class.mfct.md) · next: [11.4.2.1 The `this` pointer \[class.this\]](class.this.md)

**1** A non-static member function may be called for an object of its class type, or for an object of a class derived ([[class.derived]](class.derived.md)) from its class type, using the class member access syntax ([[expr.ref]](../expr/expr.ref.md), [[over.match.call]](../over/over.match.call.md)). A non-static member function may also be called directly using the function call syntax ([[expr.call]](../expr/expr.call.md), [[over.match.call]](../over/over.match.call.md)) from within its class or a class derived from its class, or a member thereof, as described below.

**2** If a non-static member function of a class `X` is called for an object that is not of type `X`, or of a type derived from `X`, the behavior is undefined.

**3** When an *id-expression* ([[expr.prim.id]](../expr/expr.prim.id.md)) that is not part of a class member access syntax ([[expr.ref]](../expr/expr.ref.md)) and not used to form a pointer to member ([[expr.unary.op]](../expr/expr.unary.op.md)) is used in a member of class `X` in a context where `this` can be used ([[expr.prim.this]](../expr/expr.prim.this.md)), if name lookup ([[basic.lookup]](../basic/basic.lookup.md)) resolves the name in the *id-expression* to a non-static non-type member of some class `C`, and if either the *id-expression* is potentially evaluated or `C` is `X` or a base class of `X`, the *id-expression* is transformed into a class member access expression ([[expr.ref]](../expr/expr.ref.md)) using `(*this)` ([[class.this]](class.this.md)) as the *postfix-expression* to the left of the `.` operator. [*Note 1*: If `C` is not `X` or a base class of `X`, the class member access expression is ill-formed. — *end note*] This transformation does not apply in the template definition context ([[temp.dep.type]](../temp/temp.dep.type.md)). [*Example 1*:

```cpp
struct tnode {
  char tword[20];
  int count;
  tnode* left;
  tnode* right;
  void set(const char*, tnode* l, tnode* r);
};

void tnode::set(const char* w, tnode* l, tnode* r) {
  count = strlen(w)+1;
  if (sizeof(tword)<=count)
      perror("tnode string too long");
  strcpy(tword,w);
  left = l;
  right = r;
}

void f(tnode n1, tnode n2) {
  n1.set("abc",&n2,0);
  n2.set("def",0,0);
}
```

In the body of the member function `tnode::set`, the member names `tword`, `count`, `left`, and `right` refer to members of the object for which the function is called. Thus, in the call `n1.set("abc",&n2,0)`, `tword` refers to `n1.tword`, and in the call `n2.set("def",0,0)`, it refers to `n2.tword`. The functions `strlen`, `perror`, and `strcpy` are not members of the class `tnode` and should be declared elsewhere.[^1] — *end example*]

**4** A non-static member function may be declared `const`, `volatile`, or `const` `volatile`. These *cv-qualifier*s affect the type of the `this` pointer ([[class.this]](class.this.md)). They also affect the function type ([[dcl.fct]](../dcl/dcl.fct.md)) of the member function; a member function declared `const` is a *const member function*, a member function declared `volatile` is a *volatile member function* and a member function declared `const` `volatile` is a *const volatile member function*. [*Example 2*:

```cpp
struct X {
  void g() const;
  void h() const volatile;
};
```

`X::g` is a const member function and `X::h` is a const volatile member function. — *end example*]

**5** A non-static member function may be declared with a *ref-qualifier* ([[dcl.fct]](../dcl/dcl.fct.md)); see [[over.match.funcs]](../over/over.match.funcs.md).

**6** A non-static member function may be declared virtual ([[class.virtual]](class.virtual.md)) or pure virtual ([[class.abstract]](class.abstract.md)).

## Subclauses

- [11.4.2.1 The `this` pointer \[class.this\]](class.this.md)

[^1]: See, for example, `<cstring>` ([[cstring.syn]](../lib/strings/cstring.syn.md)).
