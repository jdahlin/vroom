# 20.20.6.3 Class template `basic_format_args` [format.args]

[Contents](../../README.md) · up: [20.20.6 Arguments \[format.arguments\]](format.arguments.md) · prev: [20.20.6.2 Class template `format-arg-store` \[format.arg.store\]](format.arg.store.md) · next: [20.20.7 Class `format_error` \[format.error\]](format.error.md)

```cpp
namespace std {
  template<class Context>
  class basic_format_args {
    size_t size_;                               // exposition only
    const basic_format_arg<Context>* data_;     // exposition only

  public:
    basic_format_args() noexcept;

    template<class... Args>
      basic_format_args(const format-arg-store<Context, Args...>& store) noexcept;

    basic_format_arg<Context> get(size_t i) const noexcept;
  };
}
```

**1** An instance of `basic_format_args` provides access to formatting arguments.

```cpp
basic_format_args() noexcept;
```

**2** *Effects:* Initializes `size_` with `0`.

```cpp
template<class... Args>
  basic_format_args(const format-arg-store<Context, Args...>& store) noexcept;
```

**3** *Effects:* Initializes `size_` with `sizeof...(Args)` and `data_` with `store.args.data()`.

```cpp
basic_format_arg<Context> get(size_t i) const noexcept;
```

**4** *Returns:* `i < size_ ? data_[i]: basic_format_arg<Context>()`.

[*Note 1*: Implementations are encouraged to optimize the representation of `basic_format_args` for small number of formatting arguments by storing indices of type alternatives separately from values and packing the former. — *end note*]
