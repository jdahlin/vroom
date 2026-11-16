# 22.6.1 In general [container.adaptors.general]

[Contents](../../README.md) · up: [22.6 Container adaptors \[container.adaptors\]](container.adaptors.md) · prev: [22.6 Container adaptors \[container.adaptors\]](container.adaptors.md) · next: [22.6.2 Header `<queue>` synopsis \[queue.syn\]](queue.syn.md)

**1** The headers `<queue>` and `<stack>` define the container adaptors `queue`, `priority_queue`, and `stack`.

**2** The container adaptors each take a `Container` template parameter, and each constructor takes a `Container` reference argument. This container is copied into the `Container` member of each adaptor. If the container takes an allocator, then a compatible allocator may be passed in to the adaptor’s constructor. Otherwise, normal copy or move construction is used for the container argument. The first template parameter `T` of the container adaptors shall denote the same type as `Container::value_type`.

**3** For container adaptors, no `swap` function throws an exception unless that exception is thrown by the swap of the adaptor’s `Container` or `Compare` object (if any).

**4** A deduction guide for a container adaptor shall not participate in overload resolution if any of the following are true:

- It has an `InputIterator` template parameter and a type that does not qualify as an input iterator is deduced for that parameter.
- It has a `Compare` template parameter and a type that qualifies as an allocator is deduced for that parameter.
- It has a `Container` template parameter and a type that qualifies as an allocator is deduced for that parameter.
- It has an `Allocator` template parameter and a type that does not qualify as an allocator is deduced for that parameter.
- It has both `Container` and `Allocator` template parameters, and `uses_allocator_v<Container, Allocator>` is `false`.
