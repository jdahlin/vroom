# 9.5.2 Explicitly-defaulted functions [dcl.fct.def.default]

[Contents](../README.md) · up: [9.5 Function definitions \[dcl.fct.def\]](dcl.fct.def.md) · prev: [9.5.1 In general \[dcl.fct.def.general\]](dcl.fct.def.general.md) · next: [9.5.3 Deleted definitions \[dcl.fct.def.delete\]](dcl.fct.def.delete.md)

**1** A function definition whose *function-body* is of the form `= default;` is called an *explicitly-defaulted* definition. A function that is explicitly defaulted shall

- be a special member function or a comparison operator function ([[over.binary]](../over/over.binary.md)), and
- not have default arguments.

**2** The type `T`<sub>1</sub> of an explicitly defaulted special member function `F` is allowed to differ from the type `T`<sub>2</sub> it would have had if it were implicitly declared, as follows:

- `T`<sub>1</sub> and `T`<sub>2</sub> may have differing *ref-qualifier*s;
- `T`<sub>1</sub> and `T`<sub>2</sub> may have differing exception specifications; and
- if `T`<sub>2</sub> has a parameter of type `const C&`, the corresponding parameter of `T`<sub>1</sub> may be of type `C&`.

If `T`<sub>1</sub> differs from `T`<sub>2</sub> in any other way, then:

- if `F` is an assignment operator, and the return type of `T`<sub>1</sub> differs from the return type of `T`<sub>2</sub> or `T`<sub>1</sub>’s parameter type is not a reference, the program is ill-formed;
- otherwise, if `F` is explicitly defaulted on its first declaration, it is defined as deleted;
- otherwise, the program is ill-formed.

**3** An explicitly-defaulted function that is not defined as deleted may be declared `constexpr` or `consteval` only if it is constexpr-compatible ([[special]](../class/special.md), [[class.compare.default]](../class/class.compare.default.md)). A function explicitly defaulted on its first declaration is implicitly inline ([[dcl.inline]](dcl.inline.md)), and is implicitly constexpr ([[dcl.constexpr]](dcl.constexpr.md)) if it is constexpr-compatible.

**4** [*Example 1*:

```cpp
struct S {
  constexpr S() = default;              // error: implicit S() is not constexpr
  S(int a = 0) = default;               // error: default argument
  void operator=(const S&) = default;   // error: non-matching return type
  ~S() noexcept(false) = default;       // OK, despite mismatched exception specification
private:
  int i;
  S(S&);                                // OK: private copy constructor
};
S::S(S&) = default;                     // OK: defines copy constructor

struct T {
  T();
  T(T &&) noexcept(false);
};
struct U {
  T t;
  U();
  U(U &&) noexcept = default;
};
U u1;
U u2 = static_cast<U&&>(u1);            // OK, calls std::terminate if T::T(T&&) throws
```

— *end example*]

**5** Explicitly-defaulted functions and implicitly-declared functions are collectively called *defaulted* functions, and the implementation shall provide implicit definitions for them ([[class.ctor]](../class/class.ctor.md), [[class.dtor]](../class/class.dtor.md), [[class.copy.ctor]](../class/class.copy.ctor.md), [[class.copy.assign]](../class/class.copy.assign.md)), which might mean defining them as deleted. A defaulted prospective destructor ([[class.dtor]](../class/class.dtor.md)) that is not a destructor is defined as deleted. A defaulted special member function that is neither a prospective destructor nor an eligible special member function ([[special]](../class/special.md)) is defined as deleted. A function is *user-provided* if it is user-declared and not explicitly defaulted or deleted on its first declaration. A user-provided explicitly-defaulted function (i.e., explicitly defaulted after its first declaration) is defined at the point where it is explicitly defaulted; if such a function is implicitly defined as deleted, the program is ill-formed. [*Note 1*: Declaring a function as defaulted after its first declaration can provide efficient execution and concise definition while enabling a stable binary interface to an evolving code base. — *end note*]

**6** [*Example 2*:

```cpp
struct trivial {
  trivial() = default;
  trivial(const trivial&) = default;
  trivial(trivial&&) = default;
  trivial& operator=(const trivial&) = default;
  trivial& operator=(trivial&&) = default;
  ~trivial() = default;
};

struct nontrivial1 {
  nontrivial1();
};
nontrivial1::nontrivial1() = default;   // not first declaration
```

— *end example*]
