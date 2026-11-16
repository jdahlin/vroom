# 17.9.3 Class `bad_exception` [bad.exception]

[Contents](../README.md) · up: [17.9 Exception handling \[support.exception\]](support.exception.md) · prev: [17.9.2 Class `exception` \[exception\]](exception.md) · next: [17.9.4 Abnormal termination \[exception.terminate\]](exception.terminate.md)

```cpp
namespace std {
  class bad_exception : public exception {
  public:
    // see [exception] for the specification of the special member functions
    const char* what() const noexcept override;
  };
}
```

**1** The class `bad_exception` defines the type of the object referenced by the `exception_ptr` returned from a call to `current_exception` ([[propagation]](propagation.md)) when the currently active exception object fails to copy.

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
