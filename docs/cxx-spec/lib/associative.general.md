# 22.4.1 In general [associative.general]

[Contents](../README.md) · up: [22.4 Associative containers \[associative\]](associative.md) · prev: [22.4 Associative containers \[associative\]](associative.md) · next: [22.4.2 Header `<map>` synopsis \[associative.map.syn\]](associative.map.syn.md)

**1** The header `<map>` defines the class templates `map` and `multimap`; the header `<set>` defines the class templates `set` and `multiset`.

**2** The following exposition-only alias templates may appear in deduction guides for associative containers:

```cpp
template<class InputIterator>
  using iter-value-type =
    typename iterator_traits<InputIterator>::value_type;                // exposition only
template<class InputIterator>
  using iter-key-type = remove_const_t<
    typename iterator_traits<InputIterator>::value_type::first_type>;   // exposition only
template<class InputIterator>
  using iter-mapped-type =
    typename iterator_traits<InputIterator>::value_type::second_type;   // exposition only
template<class InputIterator>
  using iter-to-alloc-type = pair<
    add_const_t<typename iterator_traits<InputIterator>::value_type::first_type>,
    typename iterator_traits<InputIterator>::value_type::second_type>;  // exposition only
```
