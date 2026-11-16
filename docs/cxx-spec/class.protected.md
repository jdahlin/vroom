# 11.9.4 Protected member access [class.protected]

[Contents](README.md) · up: [11.9 Member access control \[class.access\]](class.access.md) · prev: [11.9.3 Friends \[class.friend\]](class.friend.md) · next: [11.9.5 Access to virtual functions \[class.access.virt\]](class.access.virt.md)

**1** An additional access check beyond those described earlier in [[class.access]](class.access.md) is applied when a non-static data member or non-static member function is a protected member of its naming class ([[class.access.base]](class.access.base.md)).[^1] As described earlier, access to a protected member is granted because the reference occurs in a friend or member of some class `C`. If the access is to form a pointer to member ([[expr.unary.op]](expr.unary.op.md)), the *nested-name-specifier* shall denote `C` or a class derived from `C`. All other accesses involve a (possibly implicit) object expression ([[expr.ref]](expr.ref.md)). In this case, the class of the object expression shall be `C` or a class derived from `C`. [*Example 1*:

```cpp
class B {
protected:
  int i;
  static int j;
};

class D1 : public B {
};

class D2 : public B {
  friend void fr(B*,D1*,D2*);
  void mem(B*,D1*);
};

void fr(B* pb, D1* p1, D2* p2) {
  pb->i = 1;                    // error
  p1->i = 2;                    // error
  p2->i = 3;                    // OK (access through a D2)
  p2->B::i = 4;                 // OK (access through a D2, even though naming class is B)
  int B::* pmi_B = &B::i;       // error
  int B::* pmi_B2 = &D2::i;     // OK (type of &D2::i is int B::*)
  B::j = 5;                     // error: not a friend of naming class B
  D2::j = 6;                    // OK (because refers to static member)
}

void D2::mem(B* pb, D1* p1) {
  pb->i = 1;                    // error
  p1->i = 2;                    // error
  i = 3;                        // OK (access through this)
  B::i = 4;                     // OK (access through this, qualification ignored)
  int B::* pmi_B = &B::i;       // error
  int B::* pmi_B2 = &D2::i;     // OK
  j = 5;                        // OK (because j refers to static member)
  B::j = 6;                     // OK (because B::j refers to static member)
}

void g(B* pb, D1* p1, D2* p2) {
  pb->i = 1;                    // error
  p1->i = 2;                    // error
  p2->i = 3;                    // error
}
```

— *end example*]

[^1]: This additional check does not apply to other members, e.g., static data members or enumerator member constants.
