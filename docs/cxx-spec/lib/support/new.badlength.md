# 17.6.3.2 Class `bad_array_new_length` [new.badlength]

[Contents](../../README.md) · up: [17.6.3 Storage allocation errors \[alloc.errors\]](alloc.errors.md) · prev: [17.6.3.1 Class `bad_alloc` \[bad.alloc\]](bad.alloc.md) · next: [17.6.3.3 Type `new_handler` \[new.handler\]](new.handler.md)

```cpp
namespace std {
  class bad_array_new_length : public bad_alloc {
  public:
    // see [exception] for the specification of the special member functions
    const char* what() const noexcept override;
  };
}
```

**1** The class `bad_array_new_length` defines the type of objects thrown as exceptions by the implementation to report an attempt to allocate an array of size less than zero or greater than an implementation-defined limit ([[expr.new]](../../expr/expr.new.md)).

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
