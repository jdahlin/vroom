# 20.7.11 Class `bad_variant_access` [variant.bad.access]

[Contents](../../README.md) · up: [20.7 Variants \[variant\]](variant.md) · prev: [20.7.10 Specialized algorithms \[variant.specalg\]](variant.specalg.md) · next: [20.7.12 Hash support \[variant.hash\]](variant.hash.md)

```cpp
class bad_variant_access : public exception {
public:
  // see [exception] for the specification of the special member functions
  const char* what() const noexcept override;
};
```

**1** Objects of type `bad_variant_access` are thrown to report invalid accesses to the value of a `variant` object.

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
