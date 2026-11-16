# 22.3.7.2 Constructors, copy, and assignment [array.cons]

[Contents](../README.md) · up: [22.3.7 Class template `array` \[array\]](array.md) · prev: [22.3.7.1 Overview \[array.overview\]](array.overview.md) · next: [22.3.7.3 Member functions \[array.members\]](array.members.md)

**1** The conditions for an aggregate ([[dcl.init.aggr]](../dcl.init.aggr.md)) shall be met. Class `array` relies on the implicitly-declared special member functions ([[class.default.ctor]](../class.default.ctor.md), [[class.dtor]](../class.dtor.md), and [[class.copy.ctor]](../class.copy.ctor.md)) to conform to the container requirements table in [[container.requirements]](container.requirements.md). In addition to the requirements specified in the container requirements table, the implicit move constructor and move assignment operator for `array` require that `T` be *Cpp17MoveConstructible* or *Cpp17MoveAssignable*, respectively.

```cpp
template<class T, class... U>
  array(T, U...) -> array<T, 1 + sizeof...(U)>;
```

**2** *Mandates:* `(is_same_v<T, U> && ...)` is `true`.
