# 19.5.4.2 Constructors [syserr.errcondition.constructors]

[Contents](../README.md) · up: [19.5.4 Class `error_condition` \[syserr.errcondition\]](syserr.errcondition.md) · prev: [19.5.4.1 Overview \[syserr.errcondition.overview\]](syserr.errcondition.overview.md) · next: [19.5.4.3 Modifiers \[syserr.errcondition.modifiers\]](syserr.errcondition.modifiers.md)

```cpp
error_condition() noexcept;
```

**1** *Postconditions:* `val_ == 0` and `cat_ == &generic_category()`.

```cpp
error_condition(int val, const error_category& cat) noexcept;
```

**2** *Postconditions:* `val_ == val` and `cat_ == &cat`.

```cpp
template<class ErrorConditionEnum>
  error_condition(ErrorConditionEnum e) noexcept;
```

**3** *Constraints:* `is_error_condition_enum_v<ErrorConditionEnum>` is `true`.

**4** *Postconditions:* `*this == make_error_condition(e)`.
