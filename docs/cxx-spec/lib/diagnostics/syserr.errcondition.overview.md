# 19.5.4.1 Overview [syserr.errcondition.overview]

[Contents](../../README.md) · up: [19.5.4 Class `error_condition` \[syserr.errcondition\]](syserr.errcondition.md) · prev: [19.5.4 Class `error_condition` \[syserr.errcondition\]](syserr.errcondition.md) · next: [19.5.4.2 Constructors \[syserr.errcondition.constructors\]](syserr.errcondition.constructors.md)

**1** The class `error_condition` describes an object used to hold values identifying error conditions. [*Note 1*: `error_condition` values are portable abstractions, while `error_code` values ([[syserr.errcode]](syserr.errcode.md)) are implementation specific. — *end note*]

```cpp
namespace std {
  class error_condition {
  public:
    // [syserr.errcondition.constructors], constructors
    error_condition() noexcept;
    error_condition(int val, const error_category& cat) noexcept;
    template<class ErrorConditionEnum>
      error_condition(ErrorConditionEnum e) noexcept;

    // [syserr.errcondition.modifiers], modifiers
    void assign(int val, const error_category& cat) noexcept;
    template<class ErrorConditionEnum>
      error_condition& operator=(ErrorConditionEnum e) noexcept;
    void clear() noexcept;

    // [syserr.errcondition.observers], observers
    int value() const noexcept;
    const error_category& category() const noexcept;
    string message() const;
    explicit operator bool() const noexcept;

  private:
    int val_;                   // exposition only
    const error_category* cat_; // exposition only
  };
}
```
