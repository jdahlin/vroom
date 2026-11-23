# 20.6.5 Class `bad_optional_access` [optional.bad.access]

[Contents](../../README.md) · up: [20.6 Optional objects \[optional\]](optional.md) · prev: [20.6.4 No-value state indicator \[optional.nullopt\]](optional.nullopt.md) · next: [20.6.6 Relational operators \[optional.relops\]](optional.relops.md)

```cpp
class bad_optional_access : public exception {
public:
  // see [exception] for the specification of the special member functions
  const char* what() const noexcept override;
};
```

**1** The class `bad_optional_access` defines the type of objects thrown as exceptions to report the situation where an attempt is made to access the value of an optional object that does not contain a value.

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
