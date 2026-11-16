# 32.9.3 Error handling [futures.errors]

[Contents](../README.md) · up: [32.9 Futures \[futures\]](futures.md) · prev: [32.9.2 Header `<future>` synopsis \[future.syn\]](future.syn.md) · next: [32.9.4 Class `future_error` \[futures.future.error\]](futures.future.error.md)

```cpp
const error_category& future_category() noexcept;
```

**1** *Returns:* A reference to an object of a type derived from class `error_category`.

**2** The object’s `default_error_condition` and equivalent virtual functions shall behave as specified for the class `error_category`. The object’s `name` virtual function returns a pointer to the string `"future"`.

```cpp
error_code make_error_code(future_errc e) noexcept;
```

**3** *Returns:* `error_code(static_cast<int>(e), future_category())`.

```cpp
error_condition make_error_condition(future_errc e) noexcept;
```

**4** *Returns:* `error_condition(static_cast<int>(e), future_category())`.
