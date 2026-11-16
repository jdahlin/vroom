# 22.6.4.5 Specialized algorithms [queue.special]

[Contents](../README.md) · up: [22.6.4 Class template `queue` \[queue\]](queue.md) · prev: [22.6.4.4 Operators \[queue.ops\]](queue.ops.md) · next: [22.6.5 Class template `priority_queue` \[priority.queue\]](priority.queue.md)

```cpp
template<class T, class Container>
  void swap(queue<T, Container>& x, queue<T, Container>& y) noexcept(noexcept(x.swap(y)));
```

**1** *Constraints:* `is_swappable_v<Container>` is `true`.

**2** *Effects:* As if by `x.swap(y)`.
