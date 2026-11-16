# 17.9.1 Header `<exception>` synopsis [exception.syn]

[Contents](../README.md) · up: [17.9 Exception handling \[support.exception\]](support.exception.md) · prev: [17.9 Exception handling \[support.exception\]](support.exception.md) · next: [17.9.2 Class `exception` \[exception\]](exception.md)

```cpp
namespace std {
  class exception;
  class bad_exception;
  class nested_exception;

  using terminate_handler = void (*)();
  terminate_handler get_terminate() noexcept;
  terminate_handler set_terminate(terminate_handler f) noexcept;
  [[noreturn]] void terminate() noexcept;

  int uncaught_exceptions() noexcept;

  using exception_ptr = unspecified;

  exception_ptr current_exception() noexcept;
  [[noreturn]] void rethrow_exception(exception_ptr p);
  template<class E> exception_ptr make_exception_ptr(E e) noexcept;

  template<class T> [[noreturn]] void throw_with_nested(T&& t);
  template<class E> void rethrow_if_nested(const E& e);
}
```
