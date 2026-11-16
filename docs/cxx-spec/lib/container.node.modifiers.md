# 22.2.4.5 Modifiers [container.node.modifiers]

[Contents](../README.md) · up: [22.2.4 Node handles \[container.node\]](container.node.md) · prev: [22.2.4.4 Observers \[container.node.observers\]](container.node.observers.md) · next: [22.2.5 Insert return type \[container.insert.return\]](container.insert.return.md)

```cpp
void swap(node-handle& nh)
  noexcept(ator_traits::propagate_on_container_swap::value ||
           ator_traits::is_always_equal::value);
```

**1** *Preconditions:* `!alloc_`, or `!nh.alloc_`, or `ator_traits::propagate_on_container_swap::value` is `true`, or `alloc_ == nh.alloc_`.

**2** *Effects:* Calls `swap(ptr_, nh.ptr_)`. If `!alloc_`, or `!nh.alloc_`, or `ator_traits::propagate_on_container_swap::value` is `true` calls `swap(alloc_, nh.alloc_)`.
