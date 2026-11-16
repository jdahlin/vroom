# 19.5.3.2 Constructors [syserr.errcode.constructors]

[Contents](../../README.md) · up: [19.5.3 Class `error_code` \[syserr.errcode\]](syserr.errcode.md) · prev: [19.5.3.1 Overview \[syserr.errcode.overview\]](syserr.errcode.overview.md) · next: [19.5.3.3 Modifiers \[syserr.errcode.modifiers\]](syserr.errcode.modifiers.md)

```cpp
error_code() noexcept;
```

**1** *Postconditions:* `val_ == 0` and `cat_ == &system_category()`.

```cpp
error_code(int val, const error_category& cat) noexcept;
```

**2** *Postconditions:* `val_ == val` and `cat_ == &cat`.

```cpp
template<class ErrorCodeEnum>
  error_code(ErrorCodeEnum e) noexcept;
```

**3** *Constraints:* `is_error_code_enum_v<ErrorCodeEnum>` is `true`.

**4** *Postconditions:* `*this == make_error_code(e)`.
