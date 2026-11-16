# 20.8.2 Class `bad_any_cast` [any.bad.any.cast]

[Contents](../README.md) · up: [20.8 Storage for any type \[any\]](any.md) · prev: [20.8.1 Header `<any>` synopsis \[any.synop\]](any.synop.md) · next: [20.8.3 Class `any` \[any.class\]](any.class.md)

```cpp
class bad_any_cast : public bad_cast {
public:
  // see [exception] for the specification of the special member functions
  const char* what() const noexcept override;
};
```

**1** Objects of type `bad_any_cast` are thrown by a failed `any_cast` ([[any.nonmembers]](any.nonmembers.md)).

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
