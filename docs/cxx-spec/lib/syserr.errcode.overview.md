# 19.5.3.1 Overview [syserr.errcode.overview]

[Contents](../README.md) · up: [19.5.3 Class `error_code` \[syserr.errcode\]](syserr.errcode.md) · prev: [19.5.3 Class `error_code` \[syserr.errcode\]](syserr.errcode.md) · next: [19.5.3.2 Constructors \[syserr.errcode.constructors\]](syserr.errcode.constructors.md)

**1** The class `error_code` describes an object used to hold error code values, such as those originating from the operating system or other low-level application program interfaces. [*Note 1*: Class `error_code` is an adjunct to error reporting by exception. — *end note*]

```cpp
namespace std {
  class error_code {
  public:
    // [syserr.errcode.constructors], constructors
    error_code() noexcept;
    error_code(int val, const error_category& cat) noexcept;
    template<class ErrorCodeEnum>
      error_code(ErrorCodeEnum e) noexcept;

    // [syserr.errcode.modifiers], modifiers
    void assign(int val, const error_category& cat) noexcept;
    template<class ErrorCodeEnum>
      error_code& operator=(ErrorCodeEnum e) noexcept;
    void clear() noexcept;

    // [syserr.errcode.observers], observers
    int value() const noexcept;
    const error_category& category() const noexcept;
    error_condition default_error_condition() const noexcept;
    string message() const;
    explicit operator bool() const noexcept;

  private:
    int val_;                   // exposition only
    const error_category* cat_; // exposition only
  };

  // [syserr.errcode.nonmembers], non-member functions
  error_code make_error_code(errc e) noexcept;

  template<class charT, class traits>
    basic_ostream<charT, traits>&
      operator<<(basic_ostream<charT, traits>& os, const error_code& ec);
}
```
