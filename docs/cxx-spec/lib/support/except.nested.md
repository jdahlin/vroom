# 17.9.7 `nested_exception` [except.nested]

[Contents](../../README.md) · up: [17.9 Exception handling \[support.exception\]](support.exception.md) · prev: [17.9.6 Exception propagation \[propagation\]](propagation.md) · next: [17.10 Initializer lists \[support.initlist\]](support.initlist.md)

```cpp
namespace std {
  class nested_exception {
  public:
    nested_exception() noexcept;
    nested_exception(const nested_exception&) noexcept = default;
    nested_exception& operator=(const nested_exception&) noexcept = default;
    virtual ~nested_exception() = default;

    // access functions
    [[noreturn]] void rethrow_nested() const;
    exception_ptr nested_ptr() const noexcept;
  };

  template<class T> [[noreturn]] void throw_with_nested(T&& t);
  template<class E> void rethrow_if_nested(const E& e);
}
```

**1** The class `nested_exception` is designed for use as a mixin through multiple inheritance. It captures the currently handled exception and stores it for later use.

**2** [*Note 1*: `nested_exception` has a virtual destructor to make it a polymorphic class. Its presence can be tested for with `dynamic_cast`. — *end note*]

```cpp
nested_exception() noexcept;
```

**3** *Effects:* The constructor calls `current_exception()` and stores the returned value.

```cpp
[[noreturn]] void rethrow_nested() const;
```

**4** *Effects:* If `nested_ptr()` returns a null pointer, the function calls the function `std::terminate`. Otherwise, it throws the stored exception captured by `*this`.

```cpp
exception_ptr nested_ptr() const noexcept;
```

**5** *Returns:* The stored exception captured by this `nested_exception` object.

```cpp
template<class T> [[noreturn]] void throw_with_nested(T&& t);
```

**6** Let `U` be `decay_t<T>`.

**7** *Preconditions:* `U` meets the *Cpp17CopyConstructible* requirements.

**8** *Throws:* If `is_class_v<U> && !is_final_v<U> && !is_base_of_v<nested_exception, U>` is `true`, an exception of unspecified type that is publicly derived from both `U` and `nested_exception` and constructed from `std::forward<T>(t)`, otherwise `std::forward<T>(t)`.

```cpp
template<class E> void rethrow_if_nested(const E& e);
```

**9** *Effects:* If `E` is not a polymorphic class type, or if `nested_exception` is an inaccessible or ambiguous base class of `E`, there is no effect. Otherwise, performs:

```cpp
if (auto p = dynamic_cast<const nested_exception*>(addressof(e)))
  p->rethrow_nested();
```
