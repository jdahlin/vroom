# 17.7.3 Class `bad_cast` [bad.cast]

[Contents](../README.md) · up: [17.7 Type identification \[support.rtti\]](support.rtti.md) · prev: [17.7.2 Class `type_info` \[type.info\]](type.info.md) · next: [17.7.4 Class `bad_typeid` \[bad.typeid\]](bad.typeid.md)

```cpp
namespace std {
  class bad_cast : public exception {
  public:
    // see [exception] for the specification of the special member functions
    const char* what() const noexcept override;
  };
}
```

**1** The class `bad_cast` defines the type of objects thrown as exceptions by the implementation to report the execution of an invalid `dynamic_cast` expression ([[expr.dynamic.cast]](../expr.dynamic.cast.md)).

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
