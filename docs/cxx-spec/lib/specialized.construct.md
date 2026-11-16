# 25.11.7 `construct_at` [specialized.construct]

[Contents](../README.md) · up: [25.11 Specialized `<memory>` algorithms \[specialized.algorithms\]](specialized.algorithms.md) · prev: [25.11.6 `uninitialized_fill` \[uninitialized.fill\]](uninitialized.fill.md) · next: [25.11.8 `destroy` \[specialized.destroy\]](specialized.destroy.md)

```cpp
template<class T, class... Args>
  constexpr T* construct_at(T* location, Args&&... args);

namespace ranges {
  template<class T, class... Args>
    constexpr T* construct_at(T* location, Args&&... args);
}
```

**1** *Constraints:* The expression `::new (declval<void*>()) T(declval<Args>()...)` is well-formed when treated as an unevaluated operand.

**2** *Effects:* Equivalent to:

```cpp
return ::new (voidify(*location)) T(std::forward<Args>(args)...);
```
