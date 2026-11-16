# 22.6.5.5 Specialized algorithms [priqueue.special]

[Contents](../../README.md) · up: [22.6.5 Class template `priority_queue` \[priority.queue\]](priority.queue.md) · prev: [22.6.5.4 Members \[priqueue.members\]](priqueue.members.md) · next: [22.6.6 Class template `stack` \[stack\]](stack.md)

```cpp
template<class T, class Container, class Compare>
  void swap(priority_queue<T, Container, Compare>& x,
            priority_queue<T, Container, Compare>& y) noexcept(noexcept(x.swap(y)));
```

**1** *Constraints:* `is_swappable_v<Container>` is `true` and `is_swappable_v<Compare>` is `true`.

**2** *Effects:* As if by `x.swap(y)`.
