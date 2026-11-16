# 17.7.4 Class `bad_typeid` [bad.typeid]

[Contents](../../README.md) · up: [17.7 Type identification \[support.rtti\]](support.rtti.md) · prev: [17.7.3 Class `bad_cast` \[bad.cast\]](bad.cast.md) · next: [17.8 Source location \[support.srcloc\]](support.srcloc.md)

```cpp
namespace std {
  class bad_typeid : public exception {
  public:
    // see [exception] for the specification of the special member functions
    const char* what() const noexcept override;
  };
}
```

**1** The class `bad_typeid` defines the type of objects thrown as exceptions by the implementation to report a null pointer in a `typeid` expression ([[expr.typeid]](../../expr/expr.typeid.md)).

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
