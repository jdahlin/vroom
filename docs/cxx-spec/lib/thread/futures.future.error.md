# 32.9.4 Class `future_error` [futures.future.error]

[Contents](../../README.md) · up: [32.9 Futures \[futures\]](futures.md) · prev: [32.9.3 Error handling \[futures.errors\]](futures.errors.md) · next: [32.9.5 Shared state \[futures.state\]](futures.state.md)

```cpp
namespace std {
  class future_error : public logic_error {
  public:
    explicit future_error(future_errc e);

    const error_code& code() const noexcept;
    const char*       what() const noexcept;

  private:
    error_code ec_;             // exposition only
  };
}
```

```cpp
explicit future_error(future_errc e);
```

**1** *Effects:* Initializes `ec_` with `make_error_code(e)`.

```cpp
const error_code& code() const noexcept;
```

**2** *Returns:* `ec_`.

```cpp
const char* what() const noexcept;
```

**3** *Returns:* An NTBS incorporating `code().message()`.
