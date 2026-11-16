# 20.14.4 Function template `invoke` [func.invoke]

[Contents](../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.3 Requirements \[func.require\]](func.require.md) · next: [20.14.5 Class template `reference_wrapper` \[refwrap\]](refwrap.md)

```cpp
template<class F, class... Args>
  constexpr invoke_result_t<F, Args...> invoke(F&& f, Args&&... args)
    noexcept(is_nothrow_invocable_v<F, Args...>);
```

**1** *Returns:* `INVOKE(std::forward<F>(f), std::forward<Args>(args)...)` ([[func.require]](func.require.md)).
