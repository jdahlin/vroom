# 20.14.5.4 Invocation [refwrap.invoke]

[Contents](../../README.md) · up: [20.14.5 Class template `reference_wrapper` \[refwrap\]](refwrap.md) · prev: [20.14.5.3 Access \[refwrap.access\]](refwrap.access.md) · next: [20.14.5.5 Helper functions \[refwrap.helpers\]](refwrap.helpers.md)

```cpp
template<class... ArgTypes>
  constexpr invoke_result_t<T&, ArgTypes...>
    operator()(ArgTypes&&... args) const;
```

**1** *Mandates:* `T` is a complete type.

**2** *Returns:* `INVOKE(get(), std::forward<ArgTypes>(args)...)`. ([[func.require]](func.require.md))
