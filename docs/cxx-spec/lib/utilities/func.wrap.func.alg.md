# 20.14.16.2.7 Specialized algorithms [func.wrap.func.alg]

[Contents](../../README.md) · up: [20.14.16.2 Class template `function` \[func.wrap.func\]](func.wrap.func.md) · prev: [20.14.16.2.6 Null pointer comparison functions \[func.wrap.func.nullptr\]](func.wrap.func.nullptr.md) · next: [20.14.17 Searchers \[func.search\]](func.search.md)

```cpp
template<class R, class... ArgTypes>
  void swap(function<R(ArgTypes...)>& f1, function<R(ArgTypes...)>& f2) noexcept;
```

**1** *Effects:* As if by: `f1.swap(f2);`
