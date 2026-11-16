# 22.2.4.2 Constructors, copy, and assignment [container.node.cons]

[Contents](../../README.md) · up: [22.2.4 Node handles \[container.node\]](container.node.md) · prev: [22.2.4.1 Overview \[container.node.overview\]](container.node.overview.md) · next: [22.2.4.3 Destructor \[container.node.dtor\]](container.node.dtor.md)

```cpp
node-handle(node-handle&& nh) noexcept;
```

**1** *Effects:* Constructs a `node-handle` object initializing `ptr_` with `nh.ptr_`. Move constructs `alloc_` with `nh.alloc_`. Assigns `nullptr` to `nh.ptr_` and assigns `nullopt` to `nh.alloc_`.

```cpp
node-handle& operator=(node-handle&& nh);
```

**2** *Preconditions:* Either `!alloc_`, or `ator_traits::propagate_on_container_move_assignment::value` is `true`, or `alloc_ == nh.alloc_`.

**3** *Effects:*

- If `ptr_ != nullptr`, destroys the `value_type` subobject in the `container_node_type` object pointed to by `ptr_` by calling `ator_traits::destroy`, then deallocates `ptr_` by calling `ator_traits::template rebind_traits<container_node_type>::deallocate`.
- Assigns `nh.ptr_` to `ptr_`.
- If `!alloc_` or `ator_traits::propagate_on_container_move_assignment::value` is `true`, move assigns `nh.alloc_` to `alloc_`.
- Assigns `nullptr` to `nh.ptr_` and assigns `nullopt` to `nh.alloc_`.

**4** *Returns:* `*this`.

**5** *Throws:* Nothing.
