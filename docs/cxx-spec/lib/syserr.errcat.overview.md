# 19.5.2.1 Overview [syserr.errcat.overview]

[Contents](../README.md) · up: [19.5.2 Class `error_category` \[syserr.errcat\]](syserr.errcat.md) · prev: [19.5.2 Class `error_category` \[syserr.errcat\]](syserr.errcat.md) · next: [19.5.2.2 Virtual members \[syserr.errcat.virtuals\]](syserr.errcat.virtuals.md)

**1** The class `error_category` serves as a base class for types used to identify the source and encoding of a particular category of error code. Classes may be derived from `error_category` to support categories of errors in addition to those defined in this document. Such classes shall behave as specified in subclause [[syserr.errcat]](syserr.errcat.md). [*Note 1*: `error_category` objects are passed by reference, and two such objects are equal if they have the same address. This means that applications using custom `error_category` types should create a single object of each such type. — *end note*]

```cpp
namespace std {
  class error_category {
  public:
    constexpr error_category() noexcept;
    virtual ~error_category();
    error_category(const error_category&) = delete;
    error_category& operator=(const error_category&) = delete;
    virtual const char* name() const noexcept = 0;
    virtual error_condition default_error_condition(int ev) const noexcept;
    virtual bool equivalent(int code, const error_condition& condition) const noexcept;
    virtual bool equivalent(const error_code& code, int condition) const noexcept;
    virtual string message(int ev) const = 0;

    bool operator==(const error_category& rhs) const noexcept;
    strong_ordering operator<=>(const error_category& rhs) const noexcept;
  };

  const error_category& generic_category() noexcept;
  const error_category& system_category() noexcept;
}
```
