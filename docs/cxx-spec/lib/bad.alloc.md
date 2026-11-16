# 17.6.3.1 Class `bad_alloc` [bad.alloc]

[Contents](../README.md) · up: [17.6.3 Storage allocation errors \[alloc.errors\]](alloc.errors.md) · prev: [17.6.3 Storage allocation errors \[alloc.errors\]](alloc.errors.md) · next: [17.6.3.2 Class `bad_array_new_length` \[new.badlength\]](new.badlength.md)

```cpp
namespace std {
  class bad_alloc : public exception {
  public:
    // see [exception] for the specification of the special member functions
    const char* what() const noexcept override;
  };
}
```

**1** The class `bad_alloc` defines the type of objects thrown as exceptions by the implementation to report a failure to allocate storage.

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
