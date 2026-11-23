# 6.9.3.2 Static initialization [basic.start.static]

[Contents](../README.md) · up: [6.9.3 Start and termination \[basic.start\]](basic.start.md) · prev: [6.9.3.1 `main` function \[basic.start.main\]](basic.start.main.md) · next: [6.9.3.3 Dynamic initialization of non-local variables \[basic.start.dynamic\]](basic.start.dynamic.md)

**1** Variables with static storage duration are initialized as a consequence of program initiation. Variables with thread storage duration are initialized as a consequence of thread execution. Within each of these phases of initiation, initialization occurs as follows.

**2** *Constant initialization* is performed if a variable or temporary object with static or thread storage duration is constant-initialized ([[expr.const]](../expr/expr.const.md)). If constant initialization is not performed, a variable with static storage duration ([[basic.stc.static]](basic.stc.static.md)) or thread storage duration ([[basic.stc.thread]](basic.stc.thread.md)) is zero-initialized ([[dcl.init]](../dcl/dcl.init.md)). Together, zero-initialization and constant initialization are called *static initialization*; all other initialization is *dynamic initialization*. All static initialization strongly happens before ([[intro.races]](intro.races.md)) any dynamic initialization. [*Note 1*: The dynamic initialization of non-local variables is described in [[basic.start.dynamic]](basic.start.dynamic.md); that of local static variables is described in [[stmt.dcl]](../stmt/stmt.dcl.md). — *end note*]

**3** An implementation is permitted to perform the initialization of a variable with static or thread storage duration as a static initialization even if such initialization is not required to be done statically, provided that

- the dynamic version of the initialization does not change the value of any other object of static or thread storage duration prior to its initialization, and
- the static version of the initialization produces the same value in the initialized variable as would be produced by the dynamic initialization if all variables not required to be initialized statically were initialized dynamically.

[*Note 2*: As a consequence, if the initialization of an object `obj1` refers to an object `obj2` of namespace scope potentially requiring dynamic initialization and defined later in the same translation unit, it is unspecified whether the value of `obj2` used will be the value of the fully initialized `obj2` (because `obj2` was statically initialized) or will be the value of `obj2` merely zero-initialized. For example,

```cpp
inline double fd() { return 1.0; }
extern double d1;
double d2 = d1;     // unspecified:
                    // may be statically initialized to 0.0 or
                    // dynamically initialized to 0.0 if d1 is
                    // dynamically initialized, or 1.0 otherwise
double d1 = fd();   // may be initialized statically or dynamically to 1.0
```

— *end note*]
