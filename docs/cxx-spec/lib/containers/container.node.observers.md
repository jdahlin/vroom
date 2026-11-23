# 22.2.4.4 Observers [container.node.observers]

[Contents](../../README.md) · up: [22.2.4 Node handles \[container.node\]](container.node.md) · prev: [22.2.4.3 Destructor \[container.node.dtor\]](container.node.dtor.md) · next: [22.2.4.5 Modifiers \[container.node.modifiers\]](container.node.modifiers.md)

```cpp
value_type& value() const;
```

**1** *Preconditions:* `empty() == false`.

**2** *Returns:* A reference to the `value_type` subobject in the `container_node_type` object pointed to by `ptr_`.

**3** *Throws:* Nothing.

```cpp
key_type& key() const;
```

**4** *Preconditions:* `empty() == false`.

**5** *Returns:* A non-const reference to the `key_type` member of the `value_type` subobject in the `container_node_type` object pointed to by `ptr_`.

**6** *Throws:* Nothing.

**7** *Remarks:* Modifying the key through the returned reference is permitted.

```cpp
mapped_type& mapped() const;
```

**8** *Preconditions:* `empty() == false`.

**9** *Returns:* A reference to the `mapped_type` member of the `value_type` subobject in the `container_node_type` object pointed to by `ptr_`.

**10** *Throws:* Nothing.

```cpp
allocator_type get_allocator() const;
```

**11** *Preconditions:* `empty() == false`.

**12** *Returns:* `*alloc_`.

**13** *Throws:* Nothing.

```cpp
explicit operator bool() const noexcept;
```

**14** *Returns:* `ptr_ != nullptr`.

```cpp
[[nodiscard]] bool empty() const noexcept;
```

**15** *Returns:* `ptr_ == nullptr`.
