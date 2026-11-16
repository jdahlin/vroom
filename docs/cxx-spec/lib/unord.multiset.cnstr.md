# 22.5.7.2 Constructors [unord.multiset.cnstr]

[Contents](../README.md) · up: [22.5.7 Class template `unordered_multiset` \[unord.multiset\]](unord.multiset.md) · prev: [22.5.7.1 Overview \[unord.multiset.overview\]](unord.multiset.overview.md) · next: [22.5.7.3 Erasure \[unord.multiset.erasure\]](unord.multiset.erasure.md)

```cpp
unordered_multiset() : unordered_multiset(size_type(see below)) { }
explicit unordered_multiset(size_type n,
                            const hasher& hf = hasher(),
                            const key_equal& eql = key_equal(),
                            const allocator_type& a = allocator_type());
```

**1** *Effects:* Constructs an empty `unordered_multiset` using the specified hash function, key equality predicate, and allocator, and using at least `n` buckets. For the default constructor, the number of buckets is implementation-defined. `max_load_factor()` returns `1.0`.

**2** *Complexity:* Constant.

```cpp
template<class InputIterator>
  unordered_multiset(InputIterator f, InputIterator l,
                     size_type n = see below,
                     const hasher& hf = hasher(),
                     const key_equal& eql = key_equal(),
                     const allocator_type& a = allocator_type());
unordered_multiset(initializer_list<value_type> il,
                   size_type n = see below,
                   const hasher& hf = hasher(),
                   const key_equal& eql = key_equal(),
                   const allocator_type& a = allocator_type());
```

**3** *Effects:* Constructs an empty `unordered_multiset` using the specified hash function, key equality predicate, and allocator, and using at least `n` buckets. If `n` is not provided, the number of buckets is implementation-defined. Then inserts elements from the range `[f, l)` for the first form, or from the range `[il.begin(), il.end())` for the second form. `max_load_factor()` returns `1.0`.

**4** *Complexity:* Average case linear, worst case quadratic.
