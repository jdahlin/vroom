# 31.8.6 Partial specializations for smart pointers [util.smartptr.atomic]

[Contents](../../README.md) · up: [31.8 Class template `atomic` \[atomics.types.generic\]](atomics.types.generic.md) · prev: [31.8.5 Member operators common to integers and pointers to objects \[atomics.types.memop\]](atomics.types.memop.md) · next: [31.8.6.1 Partial specialization for `shared_ptr` \[util.smartptr.atomic.shared\]](util.smartptr.atomic.shared.md)

**1** The library provides partial specializations of the `atomic` template for shared-ownership smart pointers ([[smartptr]](../utilities/smartptr.md)). The behavior of all operations is as specified in [[atomics.types.generic]](atomics.types.generic.md), unless specified otherwise. The template parameter `T` of these partial specializations may be an incomplete type.

**2** All changes to an atomic smart pointer in this subclause, and all associated `use_count` increments, are guaranteed to be performed atomically. Associated `use_count` decrements are sequenced after the atomic operation, but are not required to be part of it. Any associated deletion and deallocation are sequenced after the atomic update step and are not part of the atomic operation. [*Note 1*: If the atomic operation uses locks, locks acquired by the implementation will be held when any `use_count` adjustments are performed, and will not be held when any destruction or deallocation resulting from this is performed. — *end note*]

**3** [*Example 1*:

```cpp
template<typename T> class atomic_list {
  struct node {
    T t;
    shared_ptr<node> next;
  };
  atomic<shared_ptr<node>> head;

public:
  auto find(T t) const {
    auto p = head.load();
    while (p && p->t != t)
      p = p->next;

    return shared_ptr<node>(move(p));
  }

  void push_front(T t) {
    auto p = make_shared<node>();
    p->t = t;
    p->next = head;
    while (!head.compare_exchange_weak(p->next, p)) {}
  }
};
```

— *end example*]

## Subclauses

- [31.8.6.1 Partial specialization for `shared_ptr` \[util.smartptr.atomic.shared\]](util.smartptr.atomic.shared.md)
- [31.8.6.2 Partial specialization for `weak_ptr` \[util.smartptr.atomic.weak\]](util.smartptr.atomic.weak.md)
