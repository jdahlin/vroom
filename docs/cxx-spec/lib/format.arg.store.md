# 20.20.6.2 Class template `format-arg-store` [format.arg.store]

[Contents](../README.md) · up: [20.20.6 Arguments \[format.arguments\]](format.arguments.md) · prev: [20.20.6.1 Class template `basic_format_arg` \[format.arg\]](format.arg.md) · next: [20.20.6.3 Class template `basic_format_args` \[format.args\]](format.args.md)

```cpp
namespace std {
  template<class Context, class... Args>
  struct format-arg-store {      // exposition only
    array<basic_format_arg<Context>, sizeof...(Args)> args;
  };
}
```

**1** An instance of `format-arg-store` stores formatting arguments.

```cpp
template<class Context = format_context, class... Args>
  format-arg-store<Context, Args...> make_format_args(const Args&... args);
```

**2** *Preconditions:* The type `typename Context::template formatter_type<``T`<sub>*i*</sub>`>` meets the *Formatter* requirements ([[formatter.requirements]](formatter.requirements.md)) for each `T`<sub>*i*</sub> in `Args`.

**3** *Returns:* `{basic_format_arg<Context>(args)...}`.

```cpp
template<class... Args>
  format-arg-store<wformat_context, Args...> make_wformat_args(const Args&... args);
```

**4** *Effects:* Equivalent to: `return make_format_args<wformat_context>(args...);`
