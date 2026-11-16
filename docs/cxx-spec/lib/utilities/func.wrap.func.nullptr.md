# 20.14.16.2.6 Null pointer comparison functions [func.wrap.func.nullptr]

[Contents](../../README.md) · up: [20.14.16.2 Class template `function` \[func.wrap.func\]](func.wrap.func.md) · prev: [20.14.16.2.5 Target access \[func.wrap.func.targ\]](func.wrap.func.targ.md) · next: [20.14.16.2.7 Specialized algorithms \[func.wrap.func.alg\]](func.wrap.func.alg.md)

```cpp
template<class R, class... ArgTypes>
  bool operator==(const function<R(ArgTypes...)>& f, nullptr_t) noexcept;
```

**1** *Returns:* `!f`.
