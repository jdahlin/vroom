# 22.2.5 Insert return type [container.insert.return]

[Contents](../../README.md) · up: [22.2 Container requirements \[container.requirements\]](container.requirements.md) · prev: [22.2.4.5 Modifiers \[container.node.modifiers\]](container.node.modifiers.md) · next: [22.2.6 Associative containers \[associative.reqmts\]](associative.reqmts.md)

**1** The associative containers with unique keys and the unordered containers with unique keys have a member function `insert` that returns a nested type `insert_return_type`. That return type is a specialization of the template specified in this subclause.

```cpp
template<class Iterator, class NodeType>
struct insert-return-type
{
  Iterator position;
  bool     inserted;
  NodeType node;
};
```

**2** The name `insert-return-type` is exposition only. `insert-return-type` has the template parameters, data members, and special members specified above. It has no base classes or members other than those specified.
