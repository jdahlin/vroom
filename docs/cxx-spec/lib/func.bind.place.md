# 20.14.14.4 Placeholders [func.bind.place]

[Contents](../README.md) · up: [20.14.14 Function object binders \[func.bind\]](func.bind.md) · prev: [20.14.14.3 Function template `bind` \[func.bind.bind\]](func.bind.bind.md) · next: [20.14.15 Function template `mem_fn` \[func.memfn\]](func.memfn.md)

```cpp
namespace std::placeholders {
  // M is the implementation-defined number of placeholders
  see below _1;
  see below _2;
              .
              .
              .
  see below _M;
}
```

**1** All placeholder types meet the *Cpp17DefaultConstructible* and *Cpp17CopyConstructible* requirements, and their default constructors and copy/move constructors are constexpr functions that do not throw exceptions. It is implementation-defined whether placeholder types meet the *Cpp17CopyAssignable* requirements, but if so, their copy assignment operators are constexpr functions that do not throw exceptions.

**2** Placeholders should be defined as:

```cpp
inline constexpr unspecified _1{};
```

If they are not, they are declared as:

```cpp
extern unspecified _1;
```
