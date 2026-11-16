# 32.9.10.2 Globals [futures.task.nonmembers]

[Contents](../../README.md) · up: [32.9.10 Class template `packaged_task` \[futures.task\]](futures.task.md) · prev: [32.9.10.1 Member functions \[futures.task.members\]](futures.task.members.md) · next: [Annex A Grammar summary \[gram\]](../../gram/gram.md)

```cpp
template<class R, class... ArgTypes>
  void swap(packaged_task<R(ArgTypes...)>& x, packaged_task<R(ArgTypes...)>& y) noexcept;
```

**1** *Effects:* As if by `x.swap(y)`.
