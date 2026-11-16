# 19.5.7.1 Overview [syserr.syserr.overview]

[Contents](../../README.md) · up: [19.5.7 Class `system_error` \[syserr.syserr\]](syserr.syserr.md) · prev: [19.5.7 Class `system_error` \[syserr.syserr\]](syserr.syserr.md) · next: [19.5.7.2 Members \[syserr.syserr.members\]](syserr.syserr.members.md)

**1** The class `system_error` describes an exception object used to report error conditions that have an associated error code. Such error conditions typically originate from the operating system or other low-level application program interfaces.

**2** [*Note 1*: If an error represents an out-of-memory condition, implementations are encouraged to throw an exception object of type `bad_alloc` ([[bad.alloc]](../support/bad.alloc.md)) rather than `system_error`. — *end note*]

```cpp
namespace std {
  class system_error : public runtime_error {
  public:
    system_error(error_code ec, const string& what_arg);
    system_error(error_code ec, const char* what_arg);
    system_error(error_code ec);
    system_error(int ev, const error_category& ecat, const string& what_arg);
    system_error(int ev, const error_category& ecat, const char* what_arg);
    system_error(int ev, const error_category& ecat);
    const error_code& code() const noexcept;
    const char* what() const noexcept override;
  };
}
```
