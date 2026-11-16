# 22.2.4.3 Destructor [container.node.dtor]

[Contents](../../README.md) · up: [22.2.4 Node handles \[container.node\]](container.node.md) · prev: [22.2.4.2 Constructors, copy, and assignment \[container.node.cons\]](container.node.cons.md) · next: [22.2.4.4 Observers \[container.node.observers\]](container.node.observers.md)

```cpp
~node-handle();
```

**1** *Effects:* If `ptr_ != nullptr`, destroys the `value_type` subobject in the `container_node_type` object pointed to by `ptr_` by calling `ator_traits::destroy`, then deallocates `ptr_` by calling `ator_traits::template rebind_traits<container_node_type>::deallocate`.
