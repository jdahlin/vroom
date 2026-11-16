# 19.5.3.4 Observers [syserr.errcode.observers]

[Contents](../README.md) · up: [19.5.3 Class `error_code` \[syserr.errcode\]](syserr.errcode.md) · prev: [19.5.3.3 Modifiers \[syserr.errcode.modifiers\]](syserr.errcode.modifiers.md) · next: [19.5.3.5 Non-member functions \[syserr.errcode.nonmembers\]](syserr.errcode.nonmembers.md)

```cpp
int value() const noexcept;
```

**1** *Returns:* `val_`.

```cpp
const error_category& category() const noexcept;
```

**2** *Returns:* `*cat_`.

```cpp
error_condition default_error_condition() const noexcept;
```

**3** *Returns:* `category().default_error_condition(value())`.

```cpp
string message() const;
```

**4** *Returns:* `category().message(value())`.

```cpp
explicit operator bool() const noexcept;
```

**5** *Returns:* `value() != 0`.
