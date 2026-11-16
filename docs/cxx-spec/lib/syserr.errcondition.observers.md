# 19.5.4.4 Observers [syserr.errcondition.observers]

[Contents](../README.md) · up: [19.5.4 Class `error_condition` \[syserr.errcondition\]](syserr.errcondition.md) · prev: [19.5.4.3 Modifiers \[syserr.errcondition.modifiers\]](syserr.errcondition.modifiers.md) · next: [19.5.4.5 Non-member functions \[syserr.errcondition.nonmembers\]](syserr.errcondition.nonmembers.md)

```cpp
int value() const noexcept;
```

**1** *Returns:* `val_`.

```cpp
const error_category& category() const noexcept;
```

**2** *Returns:* `*cat_`.

```cpp
string message() const;
```

**3** *Returns:* `category().message(value())`.

```cpp
explicit operator bool() const noexcept;
```

**4** *Returns:* `value() != 0`.
