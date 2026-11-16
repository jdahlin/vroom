# 22.2.4.1 Overview [container.node.overview]

[Contents](../README.md) · up: [22.2.4 Node handles \[container.node\]](container.node.md) · prev: [22.2.4 Node handles \[container.node\]](container.node.md) · next: [22.2.4.2 Constructors, copy, and assignment \[container.node.cons\]](container.node.cons.md)

**1** A *node handle* is an object that accepts ownership of a single element from an associative container ([[associative.reqmts]](associative.reqmts.md)) or an unordered associative container ([[unord.req]](unord.req.md)). It may be used to transfer that ownership to another container with compatible nodes. Containers with compatible nodes have the same node handle type. Elements may be transferred in either direction between container types in the same row of [Table 79](container.node.overview.md#tab:container.node.compat).

<a id="tab:container.node.compat"></a>
**Table 79: Container types with compatible nodes** [tab:container.node.compat]

|  |  |
| --- | --- |
| `map<K, T, C1, A>` | `map<K, T, C2, A>` |
| `map<K, T, C1, A>` | `multimap<K, T, C2, A>` |
| `set<K, C1, A>` | `set<K, C2, A>` |
| `set<K, C1, A>` | `multiset<K, C2, A>` |
| `unordered_map<K, T, H1, E1, A>` | `unordered_map<K, T, H2, E2, A>` |
| `unordered_map<K, T, H1, E1, A>` | `unordered_multimap<K, T, H2, E2, A>` |
| `unordered_set<K, H1, E1, A>` | `unordered_set<K, H2, E2, A>` |
| `unordered_set<K, H1, E1, A>` | `unordered_multiset<K, H2, E2, A>` |

**2** If a node handle is not empty, then it contains an allocator that is equal to the allocator of the container when the element was extracted. If a node handle is empty, it contains no allocator.

**3** Class `node-handle` is for exposition only.

**4** If a user-defined specialization of `pair` exists for `pair<const Key, T>` or `pair<Key, T>`, where `Key` is the container’s `key_type` and `T` is the container’s `mapped_type`, the behavior of operations involving node handles is undefined.

```cpp
template<unspecified>
class node-handle {
public:
  // These type declarations are described in Tables [tab:container.assoc.req] and [tab:container.hash.req].
  using value_type     = see below;     // not present for map containers
  using key_type       = see below;     // not present for set containers
  using mapped_type    = see below;     // not present for set containers
  using allocator_type = see below;

private:
  using container_node_type = unspecified;
  using ator_traits = allocator_traits<allocator_type>;

  typename ator_traits::template rebind_traits<container_node_type>::pointer ptr_;
  optional<allocator_type> alloc_;

public:
  // [container.node.cons], constructors, copy, and assignment
  constexpr node-handle() noexcept : ptr_(), alloc_() {}
  node-handle(node-handle&&) noexcept;
  node-handle& operator=(node-handle&&);

  // [container.node.dtor], destructor
  ~node-handle();

  // [container.node.observers], observers
  value_type& value() const;            // not present for map containers
  key_type& key() const;                // not present for set containers
  mapped_type& mapped() const;          // not present for set containers

  allocator_type get_allocator() const;
  explicit operator bool() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  // [container.node.modifiers], modifiers
  void swap(node-handle&)
    noexcept(ator_traits::propagate_on_container_swap::value ||
             ator_traits::is_always_equal::value);

  friend void swap(node-handle& x, node-handle& y) noexcept(noexcept(x.swap(y))) {
    x.swap(y);
  }
};
```
