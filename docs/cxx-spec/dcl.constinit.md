# 9.2.6 The `constinit` specifier [dcl.constinit]

[Contents](README.md) · up: [9.2 Specifiers \[dcl.spec\]](dcl.spec.md) · prev: [9.2.5 The `constexpr` and `consteval` specifiers \[dcl.constexpr\]](dcl.constexpr.md) · next: [9.2.7 The `inline` specifier \[dcl.inline\]](dcl.inline.md)

**1** The `constinit` specifier shall be applied only to a declaration of a variable with static or thread storage duration. If the specifier is applied to any declaration of a variable, it shall be applied to the initializing declaration. No diagnostic is required if no `constinit` declaration is reachable at the point of the initializing declaration.

**2** If a variable declared with the `constinit` specifier has dynamic initialization ([[basic.start.dynamic]](basic.start.dynamic.md)), the program is ill-formed. [*Note 1*: The `constinit` specifier ensures that the variable is initialized during static initialization ([[basic.start.static]](basic.start.static.md)). — *end note*]

**3** [*Example 1*:

```cpp
const char * g() { return "dynamic initialization"; }
constexpr const char * f(bool p) { return p ? "constant initializer" : g(); }
constinit const char * c = f(true);     // OK
constinit const char * d = f(false);    // error
```

— *end example*]
