# 11.9.6 Multiple access [class.paths]

[Contents](README.md) · up: [11.9 Member access control \[class.access\]](class.access.md) · prev: [11.9.5 Access to virtual functions \[class.access.virt\]](class.access.virt.md) · next: [11.9.7 Nested classes \[class.access.nest\]](class.access.nest.md)

**1** If a name can be reached by several paths through a multiple inheritance graph, the access is that of the path that gives most access. [*Example 1*:

```cpp
class W { public: void f(); };
class A : private virtual W { };
class B : public virtual W { };
class C : public A, public B {
  void f() { W::f(); }          // OK
};
```

Since `W::f()` is available to `C::f()` along the public path through `B`, access is allowed. — *end example*]
