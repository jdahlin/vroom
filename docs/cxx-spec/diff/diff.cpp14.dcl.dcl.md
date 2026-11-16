# C.2.3 [[dcl.dcl]](../dcl/dcl.dcl.md): declarations [diff.cpp14.dcl.dcl]

[Contents](../README.md) · up: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · prev: [C.2.2 [[expr]](../expr/expr.md): expressions \[diff.cpp14.expr\]](diff.cpp14.expr.md) · next: [C.2.4 [[class]](../class/class.md): classes \[diff.cpp14.class\]](diff.cpp14.class.md)

**1** **Affected subclause:** [[dcl.stc]](../dcl/dcl.stc.md) <br>**Change:** Removal of `register` *storage-class-specifier*. <br>**Rationale:** Enable repurposing of deprecated keyword in future revisions of this International Standard. <br>**Effect on original feature:** A valid C++ 2014 declaration utilizing the `register` *storage-class-specifier* is ill-formed in this International Standard. The specifier can simply be removed to retain the original meaning.

**2** **Affected subclause:** [[dcl.spec.auto]](../dcl/dcl.spec.auto.md) <br>**Change:** `auto` deduction from *braced-init-list*. <br>**Rationale:** More intuitive deduction behavior. <br>**Effect on original feature:** Valid C++ 2014 code may fail to compile or may change meaning in this International Standard. For example:

```cpp
auto x1{1};         // was std::initializer_list<int>, now int
auto x2{1, 2};      // was std::initializer_list<int>, now ill-formed
```

**3** **Affected subclause:** [[dcl.fct]](../dcl/dcl.fct.md) <br>**Change:** Make exception specifications be part of the type system. <br>**Rationale:** Improve type-safety. <br>**Effect on original feature:** Valid C++ 2014 code may fail to compile or change meaning in this International Standard. For example:

```cpp
void g1() noexcept;
void g2();
template<class T> int f(T *, T *);
int x = f(g1, g2);              // ill-formed; previously well-formed
```

**4** **Affected subclause:** [[dcl.init.aggr]](../dcl/dcl.init.aggr.md) <br>**Change:** Definition of an aggregate is extended to apply to user-defined types with base classes. <br>**Rationale:** To increase convenience of aggregate initialization. <br>**Effect on original feature:** Valid C++ 2014 code may fail to compile or produce different results in this International Standard; initialization from an empty initializer list will perform aggregate initialization instead of invoking a default constructor for the affected types. For example:

```cpp
struct derived;
struct base {
  friend struct derived;
private:
  base();
};
struct derived : base {};

derived d1{};       // error; the code was well-formed in C++ 2014
derived d2;         // still OK
```
