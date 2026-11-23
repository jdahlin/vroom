# 22.3.1 In general [sequences.general]

[Contents](../../README.md) · up: [22.3 Sequence containers \[sequences\]](sequences.md) · prev: [22.3 Sequence containers \[sequences\]](sequences.md) · next: [22.3.2 Header `<array>` synopsis \[array.syn\]](array.syn.md)

**1** The headers `<array>` ([[array.syn]](array.syn.md)), `<deque>` ([[deque.syn]](deque.syn.md)), `<forward_list>` ([[forward.list.syn]](forward.list.syn.md)), `<list>` ([[list.syn]](list.syn.md)), and `<vector>` ([[vector.syn]](vector.syn.md)) define class templates that meet the requirements for sequence containers.

**2** The following exposition-only alias template may appear in deduction guides for sequence containers:

```cpp
template<class InputIterator>
  using iter-value-type = typename iterator_traits<InputIterator>::value_type;  // exposition only
```
