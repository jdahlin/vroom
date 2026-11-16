# 11.9.5 Access to virtual functions [class.access.virt]

[Contents](README.md) · up: [11.9 Member access control \[class.access\]](class.access.md) · prev: [11.9.4 Protected member access \[class.protected\]](class.protected.md) · next: [11.9.6 Multiple access \[class.paths\]](class.paths.md)

**1** The access rules ([[class.access]](class.access.md)) for a virtual function are determined by its declaration and are not affected by the rules for a function that later overrides it. [*Example 1*:

```cpp
class B {
public:
  virtual int f();
};

class D : public B {
private:
  int f();
};

void f() {
  D d;
  B* pb = &d;
  D* pd = &d;

  pb->f();                      // OK: B::f() is public, D::f() is invoked
  pd->f();                      // error: D::f() is private
}
```

— *end example*]

**2** Access is checked at the call point using the type of the expression used to denote the object for which the member function is called (`B*` in the example above). The access of the member function in the class in which it was defined (`D` in the example above) is in general not known.
