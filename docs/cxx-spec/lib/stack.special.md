# 22.6.6.5 Specialized algorithms [stack.special]

[Contents](../README.md) · up: [22.6.6 Class template `stack` \[stack\]](stack.md) · prev: [22.6.6.4 Operators \[stack.ops\]](stack.ops.md) · next: [22.7 Views \[views\]](views.md)

```cpp
template<class T, class Container>
  void swap(stack<T, Container>& x, stack<T, Container>& y) noexcept(noexcept(x.swap(y)));
```

**1** *Constraints:* `is_swappable_v<Container>` is `true`.

**2** *Effects:* As if by `x.swap(y)`.
