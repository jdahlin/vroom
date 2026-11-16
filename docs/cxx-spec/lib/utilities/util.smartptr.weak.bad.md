# 20.11.2 Class `bad_weak_ptr` [util.smartptr.weak.bad]

[Contents](../../README.md) · up: [20.11 Smart pointers \[smartptr\]](smartptr.md) · prev: [20.11.1.6 I/O \[unique.ptr.io\]](unique.ptr.io.md) · next: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md)

```cpp
namespace std {
  class bad_weak_ptr : public exception {
  public:
    // see [exception] for the specification of the special member functions
    const char* what() const noexcept override;
  };
}
```

**1** An exception of type `bad_weak_ptr` is thrown by the `shared_ptr` constructor taking a `weak_ptr`.

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
