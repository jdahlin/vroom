# 20.14.15 Function template `mem_fn` [func.memfn]

[Contents](../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.14.4 Placeholders \[func.bind.place\]](func.bind.place.md) · next: [20.14.16 Polymorphic function wrappers \[func.wrap\]](func.wrap.md)

```cpp
template<class R, class T> constexpr unspecified mem_fn(R T::* pm) noexcept;
```

**1** *Returns:* A simple call wrapper ([[func.def]](func.def.md)) `fn` with call pattern `invoke(pmd, call_args...)`, where `pmd` is the target object of `fn` of type `R T::*` direct-non-list-initialized with `pm`, and `call_args` is an argument pack used in a function call expression ([[expr.call]](../expr.call.md)) of `pm`.
