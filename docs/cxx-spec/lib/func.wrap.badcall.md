# 20.14.16.1 Class `bad_function_call` [func.wrap.badcall]

[Contents](../README.md) · up: [20.14.16 Polymorphic function wrappers \[func.wrap\]](func.wrap.md) · prev: [20.14.16 Polymorphic function wrappers \[func.wrap\]](func.wrap.md) · next: [20.14.16.2 Class template `function` \[func.wrap.func\]](func.wrap.func.md)

**1** An exception of type `bad_function_call` is thrown by `function::operator()` ([[func.wrap.func.inv]](func.wrap.func.inv.md)) when the function wrapper object has no target.

```cpp
namespace std {
  class bad_function_call : public exception {
  public:
    // see [exception] for the specification of the special member functions
    const char* what() const noexcept override;
  };
}
```

```cpp
const char* what() const noexcept override;
```

**2** *Returns:* An implementation-defined NTBS.
