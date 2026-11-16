# 19.5.3.3 Modifiers [syserr.errcode.modifiers]

[Contents](../../README.md) · up: [19.5.3 Class `error_code` \[syserr.errcode\]](syserr.errcode.md) · prev: [19.5.3.2 Constructors \[syserr.errcode.constructors\]](syserr.errcode.constructors.md) · next: [19.5.3.4 Observers \[syserr.errcode.observers\]](syserr.errcode.observers.md)

```cpp
void assign(int val, const error_category& cat) noexcept;
```

**1** *Postconditions:* `val_ == val` and `cat_ == &cat`.

```cpp
template<class ErrorCodeEnum>
  error_code& operator=(ErrorCodeEnum e) noexcept;
```

**2** *Constraints:* `is_error_code_enum_v<ErrorCodeEnum>` is `true`.

**3** *Postconditions:* `*this == make_error_code(e)`.

**4** *Returns:* `*this`.

```cpp
void clear() noexcept;
```

**5** *Postconditions:* `value() == 0` and `category() == system_category()`.
