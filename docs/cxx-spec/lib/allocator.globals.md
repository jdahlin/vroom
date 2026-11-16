# 20.10.10.2 Operators [allocator.globals]

[Contents](../README.md) · up: [20.10.10 The default allocator \[default.allocator\]](default.allocator.md) · prev: [20.10.10.1 Members \[allocator.members\]](allocator.members.md) · next: [20.10.11 `addressof` \[specialized.addressof\]](specialized.addressof.md)

```cpp
template<class T, class U>
  constexpr bool operator==(const allocator<T>&, const allocator<U>&) noexcept;
```

**1** *Returns:* `true`.
