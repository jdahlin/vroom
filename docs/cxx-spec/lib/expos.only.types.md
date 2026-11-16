# 16.4.2.2.2 Exposition-only types [expos.only.types]

[Contents](../README.md) · up: [16.4.2.2 Type descriptions \[type.descriptions\]](type.descriptions.md) · prev: [16.4.2.2.1 General \[type.descriptions.general\]](type.descriptions.general.md) · next: [16.4.2.2.3 Enumerated types \[enumerated.types\]](enumerated.types.md)

**1** Several types defined in [[support]](support.md) through [[thread]](thread.md) and [[depr]](../depr.md) are defined for the purpose of exposition. The declaration of such a type is followed by a comment ending in *exposition only*. [*Example 1*:

```cpp
namespace std {
  extern "C" using some-handler = int(int, void*, double);  // exposition only
}
```

The type placeholder `some-handler` can now be used to specify a function that takes a callback parameter with C language linkage. — *end example*]
