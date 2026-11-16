# 22.2.6 Associative containers [associative.reqmts]

[Contents](../README.md) · up: [22.2 Container requirements \[container.requirements\]](container.requirements.md) · prev: [22.2.5 Insert return type \[container.insert.return\]](container.insert.return.md) · next: [22.2.6.1 Exception safety guarantees \[associative.reqmts.except\]](associative.reqmts.except.md)

**1** Associative containers provide fast retrieval of data based on keys. The library provides four basic kinds of associative containers: `set`, `multiset`, `map` and `multimap`.

**2** Each associative container is parameterized on `Key` and an ordering relation `Compare` that induces a strict weak ordering ([[alg.sorting]](alg.sorting.md)) on elements of `Key`. In addition, `map` and `multimap` associate an arbitrary *mapped type* `T` with the `Key`. The object of type `Compare` is called the *comparison object* of a container.

**3** The phrase “equivalence of keys” means the equivalence relation imposed by the comparison object. That is, two keys `k1` and `k2` are considered to be equivalent if for the comparison object `comp`, `comp(k1, k2) == false && comp(k2, k1) == false`. [*Note 1*: This is not necessarily the same as the result of `k1 == k2`. — *end note*] For any two keys `k1` and `k2` in the same container, calling `comp(k1, k2)` shall always return the same value.

**4** An associative container supports *unique keys* if it may contain at most one element for each key. Otherwise, it supports *equivalent keys*. The `set` and `map` classes support unique keys; the `multiset` and `multimap` classes support equivalent keys. For `multiset` and `multimap`, `insert`, `emplace`, and `erase` preserve the relative ordering of equivalent elements.

**5** For `set` and `multiset` the value type is the same as the key type. For `map` and `multimap` it is equal to `pair<const Key, T>`.

**6** `iterator` of an associative container is of the bidirectional iterator category. For associative containers where the value type is the same as the key type, both `iterator` and `const_iterator` are constant iterators. It is unspecified whether or not `iterator` and `const_iterator` are the same type. [*Note 2*: `iterator` and `const_iterator` have identical semantics in this case, and `iterator` is convertible to `const_iterator`. Users can avoid violating the one-definition rule by always using `const_iterator` in their function parameter lists. — *end note*]

**7** The associative containers meet all the requirements of Allocator-aware containers ([[container.requirements.general]](container.requirements.general.md)), except that for `map` and `multimap`, the requirements placed on `value_type` in [Table 76](container.requirements.general.md#tab:container.alloc.req) apply instead to `key_type` and `mapped_type`. [*Note 3*: For example, in some cases `key_type` and `mapped_type` are required to be *Cpp17CopyAssignable* even though the associated `value_type`, `pair<const key_type, mapped_type>`, is not *Cpp17CopyAssignable*. — *end note*]

**8** In [Table 80](associative.reqmts.md#tab:container.assoc.req), `X` denotes an associative container class, `a` denotes a value of type `X`, `a2` denotes a value of a type with nodes compatible with type `X` ([Table 79](container.node.overview.md#tab:container.node.compat)), `b` denotes a possibly `const` value of type `X`, `u` denotes the name of a variable being declared, `a_uniq` denotes a value of type `X` when `X` supports unique keys, `a_eq` denotes a value of type `X` when `X` supports multiple keys, `a_tran` denotes a possibly `const` value of type `X` when the *qualified-id* `X::key_compare::is_transparent` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)), `i` and `j` meet the *Cpp17InputIterator* requirements and refer to elements implicitly convertible to `value_type`, `[i, j)` denotes a valid range, `p` denotes a valid constant iterator to `a`, `q` denotes a valid dereferenceable constant iterator to `a`, `r` denotes a valid dereferenceable iterator to `a`, `[q1, q2)` denotes a valid range of constant iterators in `a`, `il` designates an object of type `initializer_list<value_type>`, `t` denotes a value of type `X::value_type`, `k` denotes a value of type `X::key_type` and `c` denotes a possibly `const` value of type `X::key_compare`; `kl` is a value such that `a` is partitioned ([[alg.sorting]](alg.sorting.md)) with respect to `c(r, kl)`, with `r` the key value of `e` and `e` in `a`; `ku` is a value such that `a` is partitioned with respect to `!c(ku, r)`; `ke` is a value such that `a` is partitioned with respect to `c(r, ke)` and `!c(ke, r)`, with `c(r, ke)` implying `!c(ke, r)`. `A` denotes the storage allocator used by `X`, if any, or `allocator<X::value_type>` otherwise, `m` denotes an allocator of a type convertible to `A`, and `nh` denotes a non-const rvalue of type `X::node_type`.

<a id="tab:container.assoc.req"></a>
**Table 80: Associative container requirements (in addition to container)** [tab:container.assoc.req]

| Expression | Return type | Assertion/note | Complexity |
| --- | --- | --- | --- |
|  |  | pre-/post-condition |  |
| `X::key_type` | `Key` |  | compile time |
| `X::mapped_type` (`map` and `multimap` only) | `T` |  | compile time |
| `X::value_type` (`set` and `multiset` only) | `Key` | *Preconditions:* `value_type` is *Cpp17Erasable* from `X` | compile time |
| `X::value_type` (`map` and `multimap` only) | `pair<const Key, T>` | *Preconditions:* `value_type` is *Cpp17Erasable* from `X` | compile time |
| `X::key_compare` | `Compare` | *Preconditions:* `key_compare` is *Cpp17CopyConstructible*. | compile time |
| `X::value_compare` | a binary predicate type | is the same as `key_compare` for `set` and `multiset`; is an ordering relation on pairs induced by the first component (i.e., `Key`) for `map` and `multimap`. | compile time |
| `X::node_type` | a specialization of a `node-handle` class template, such that the public nested types are the same types as the corresponding types in `X`. | see [[container.node]](container.node.md) | compile time |
| `X(c)`<br>`X u(c);` |  | *Effects:* Constructs an empty container. Uses a copy of `c` as a comparison object. | constant |
| `X()`<br>`X u;` |  | *Preconditions:* `key_compare` meets the *Cpp17DefaultConstructible* requirements.<br>*Effects:* Constructs an empty container. Uses `Compare()` as a comparison object | constant |
| `X(i,j,c)`<br>`X u(i,j,c);` |  | *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `X` from `*i`.<br>*Effects:* Constructs an empty container and inserts elements from the range `[i, j)` into it; uses `c` as a comparison object. | *N* log *N* in general, where *N* has the value `distance(i, j)`; linear if `[i, j)` is sorted with `value_comp()` |
| `X(i,j)`<br>`X u(i,j);` |  | *Preconditions:* `key_compare` meets the *Cpp17DefaultConstructible* requirements. `value_type` is *Cpp17EmplaceConstructible* into `X` from `*i`.<br>*Effects:* Same as above, but uses `Compare()` as a comparison object. | same as above |
| `X(il)` |  | same as `X(il.begin(), il.end())` | same as `X(il.begin(), il.end())` |
| `X(il,c)` |  | same as `X(il.begin(), il.end(), c)` | same as `X(il.begin(), il.end(), c)` |
| `a = il` | `X&` | *Preconditions:* `value_type` is *Cpp17CopyInsertable* into `X` and *Cpp17CopyAssignable*.<br>*Effects:* Assigns the range `[il.begin(), il.end())` into `a`. All existing elements of `a` are either assigned to or destroyed. | *N* log *N* in general, where *N* has the value `il.size() + a.size()`; linear if `[il.begin(), il.end())` is sorted with `value_comp()` |
| `b.key_comp()` | `X::key_compare` | *Returns:* the comparison object out of which `b` was constructed. | constant |
| `b.value_comp()` | `X::value_compare` | *Returns:* an object of `value_compare` constructed out of the comparison object | constant |
| `a_uniq.emplace(args)` | `pair<iterator, bool>` | *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `X` from `args`.<br>*Effects:* Inserts a `value_type` object `t` constructed with `std::forward<Args>(args)...` if and only if there is no element in the container with key equivalent to the key of `t`. The `bool` component of the returned pair is `true` if and only if the insertion takes place, and the iterator component of the pair points to the element with key equivalent to the key of `t`. | logarithmic |
| `a_eq.emplace(args)` | `iterator` | *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `X` from `args`.<br>*Effects:* Inserts a `value_type` object `t` constructed with `std::forward<Args>(args)...` and returns the iterator pointing to the newly inserted element. If a range containing elements equivalent to `t` exists in `a_eq`, `t` is inserted at the end of that range. | logarithmic |
| `a.emplace_hint(p, args)` | `iterator` | equivalent to `a.emplace(` `std::forward<Args>(args)...)`. Return value is an iterator pointing to the element with the key equivalent to the newly inserted element. The element is inserted as close as possible to the position just prior to `p`. | logarithmic in general, but amortized constant if the element is inserted right before `p` |
| `a_uniq.insert(t)` | `pair<iterator, bool>` | *Preconditions:* If `t` is a non-const rvalue, `value_type` is *Cpp17MoveInsertable* into `X`; otherwise, `value_type` is *Cpp17CopyInsertable* into `X`.<br>*Effects:* Inserts `t` if and only if there is no element in the container with key equivalent to the key of `t`. The `bool` component of the returned pair is `true` if and only if the insertion takes place, and the `iterator` component of the pair points to the element with key equivalent to the key of `t`. | logarithmic |
| `a_eq.insert(t)` | `iterator` | *Preconditions:* If `t` is a non-const rvalue, `value_type` is *Cpp17MoveInsertable* into `X`; otherwise, `value_type` is *Cpp17CopyInsertable* into `X`.<br>*Effects:* Inserts `t` and returns the iterator pointing to the newly inserted element. If a range containing elements equivalent to `t` exists in `a_eq`, `t` is inserted at the end of that range. | logarithmic |
| `a.insert(p, t)` | `iterator` | *Preconditions:* If `t` is a non-const rvalue, `value_type` is *Cpp17MoveInsertable* into `X`; otherwise, `value_type` is *Cpp17CopyInsertable* into `X`.<br>*Effects:* Inserts `t` if and only if there is no element with key equivalent to the key of `t` in containers with unique keys; always inserts `t` in containers with equivalent keys. Always returns the iterator pointing to the element with key equivalent to the key of `t`. `t` is inserted as close as possible to the position just prior to `p`. | logarithmic in general, but amortized constant if `t` is inserted right before `p`. |
| `a.insert(i, j)` | `void` | *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `X` from `*i`. Neither `i` nor `j` are iterators into `a`.<br>*Effects:* Inserts each element from the range `[i, j)` if and only if there is no element with key equivalent to the key of that element in containers with unique keys; always inserts that element in containers with equivalent keys. | *N* log (`a.size()` + *N*), where *N* has the value `distance(i, j)` |
| `a.insert(il)` | `void` | equivalent to `a.insert(il.begin(), il.end())` |  |
| `a_uniq.insert(nh)` | `insert_return_type` | *Preconditions:* `nh` is empty or `a_uniq.get_allocator() == nh.get_allocator()`.<br>*Effects:* If `nh` is empty, has no effect. Otherwise, inserts the element owned by `nh` if and only if there is no element in the container with a key equivalent to `nh.key()`.<br>*Postconditions:* If `nh` is empty, `inserted` is `false`, `position` is `end()`, and `node` is empty. Otherwise if the insertion took place, `inserted` is `true`, `position` points to the inserted element, and `node` is empty; if the insertion failed, `inserted` is `false`, `node` has the previous value of `nh`, and `position` points to an element with a key equivalent to `nh.key()`. | logarithmic |
| `a_eq.insert(nh)` | `iterator` | *Preconditions:* `nh` is empty or `a_eq.get_allocator() == nh.get_allocator()`.<br>*Effects:* If `nh` is empty, has no effect and returns `a_eq.end()`. Otherwise, inserts the element owned by `nh` and returns an iterator pointing to the newly inserted element. If a range containing elements with keys equivalent to `nh.key()` exists in `a_eq`, the element is inserted at the end of that range.<br>*Postconditions:* `nh` is empty. | logarithmic |
| `a.insert(p, nh)` | `iterator` | *Preconditions:* `nh` is empty or `a.get_allocator() == nh.get_allocator()`.<br>*Effects:* If `nh` is empty, has no effect and returns `a.end()`. Otherwise, inserts the element owned by `nh` if and only if there is no element with key equivalent to `nh.key()` in containers with unique keys; always inserts the element owned by `nh` in containers with equivalent keys. Always returns the iterator pointing to the element with key equivalent to `nh.key()`. The element is inserted as close as possible to the position just prior to `p`.<br>*Postconditions:* `nh` is empty if insertion succeeds, unchanged if insertion fails. | logarithmic in general, but amortized constant if the element is inserted right before `p`. |
| `a.extract(k)` | `node_type` | *Effects:* Removes the first element in the container with key equivalent to `k`.<br>*Returns:* A `node_type` owning the element if found, otherwise an empty `node_type`. | log (`a.size()`) |
| `a.extract(q)` | `node_type` | *Effects:* Removes the element pointed to by `q`.<br>*Returns:* A `node_type` owning that element. | amortized constant |
| `a.merge(a2)` | `void` | *Preconditions:* `a.get_allocator() == a2.get_allocator()`.<br>*Effects:* Attempts to extract each element in `a2` and insert it into `a` using the comparison object of `a`. In containers with unique keys, if there is an element in `a` with key equivalent to the key of an element from `a2`, then that element is not extracted from `a2`.<br>*Postconditions:* Pointers and references to the transferred elements of `a2` refer to those same elements but as members of `a`. Iterators referring to the transferred elements will continue to refer to their elements, but they now behave as iterators into `a`, not into `a2`.<br>*Throws:* Nothing unless the comparison object throws. | *N* log(`a.size()+` *N*), where *N* has the value `a2.size()`. |
| `a.erase(k)` | `size_type` | *Effects:* Erases all elements in the container with key equivalent to `k`.<br>*Returns:* The number of erased elements. | log (`a.size()`) + `a.count(k)` |
| `a.erase(q)` | `iterator` | *Effects:* Erases the element pointed to by `q`.<br>*Returns:* An iterator pointing to the element immediately following `q` prior to the element being erased. If no such element exists, returns `a.end()`. | amortized constant |
| `a.erase(r)` | `iterator` | *Effects:* Erases the element pointed to by `r`.<br>*Returns:* An iterator pointing to the element immediately following `r` prior to the element being erased. If no such element exists, returns `a.end()`. | amortized constant |
| `a.erase(`<br>`q1, q2)` | `iterator` | *Effects:* Erases all the elements in the range `[q1, q2)`.<br>*Returns:* An iterator pointing to the element pointed to by `q2` prior to any elements being erased. If no such element exists, `a.end()` is returned. | log(`a.size()`) + *N*, where *N* has the value `distance(q1, q2)`. |
| `a.clear()` | `void` | *Effects:* Equivalent to `a.erase(a.begin(), a.end())`.<br>*Postconditions:* `a.empty()` is `true`. | linear in `a.size()`. |
| `b.find(k)` | `iterator`; `const_iterator` for constant `b`. | *Returns:* An iterator pointing to an element with the key equivalent to `k`, or `b.end()` if such an element is not found. | logarithmic |
| `a_tran.`<br>`find(ke)` | `iterator`; `const_iterator` for constant `a_tran`. | *Returns:* An iterator pointing to an element with key `r` such that `!c(r, ke) && !c(ke, r)`, or `a_tran.end()` if such an element is not found. | logarithmic |
| `b.count(k)` | `size_type` | *Returns:* The number of elements with key equivalent to `k`. | log (`b.size()`) + `b.count(k)` |
| `a_tran.`<br>`count(ke)` | `size_type` | *Returns:* The number of elements with key `r` such that `!c(r, ke) && !c(ke, r)` | log (`a_tran.size()`) + `a_tran.count(ke)` |
| `b.`<br>`contains(k)` | `bool` | *Effects:* Equivalent to: `return b.find(k) != b.end();` | logarithmic |
| `a_tran.`<br>`contains(ke)` | `bool` | *Effects:* Equivalent to: `return a_tran.find(ke) != a_tran.end();` | logarithmic |
| `b.lower_bound(k)` | `iterator`; `const_iterator` for constant `b`. | *Returns:* An iterator pointing to the first element with key not less than `k`, or `b.end()` if such an element is not found. | logarithmic |
| `a_tran.`<br>`lower_bound(kl)` | `iterator`; `const_iterator` for constant `a_tran`. | *Returns:* An iterator pointing to the first element with key `r` such that `!c(r, kl)`, or `a_tran.end()` if such an element is not found. | logarithmic |
| `b.upper_bound(k)` | `iterator`; `const_iterator` for constant `b`. | *Returns:* An iterator pointing to the first element with key greater than `k`, or `b.end()` if such an element is not found. | logarithmic |
| `a_tran.`<br>`upper_bound(ku)` | `iterator`; `const_iterator` for constant `a_tran`. | *Returns:* An iterator pointing to the first element with key `r` such that `c(ku, r)`, or `a_tran.end()` if such an element is not found. | logarithmic |
| `b.equal_range(k)` | `pair<iterator, iterator>`; `pair<const_iterator, const_iterator>` for constant `b`. | *Effects:* Equivalent to: `return make_pair(b.lower_bound(k), b.upper_bound(k));` | logarithmic |
| `a_tran.`<br>`equal_range(ke)` | `pair<iterator, iterator>`; `pair<const_iterator, const_iterator>` for constant `a_tran`. | *Effects:* Equivalent to: `return make_pair(`<br>`a_tran.lower_bound(ke), a_tran.upper_bound(ke));` | logarithmic |

**9** The `insert` and `emplace` members shall not affect the validity of iterators and references to the container, and the `erase` members shall invalidate only iterators and references to the erased elements.

**10** The `extract` members invalidate only iterators to the removed element; pointers and references to the removed element remain valid. However, accessing the element through such pointers and references while the element is owned by a `node_type` is undefined behavior. References and pointers to an element obtained while it is owned by a `node_type` are invalidated if the element is successfully inserted.

**11** The fundamental property of iterators of associative containers is that they iterate through the containers in the non-descending order of keys where non-descending is defined by the comparison that was used to construct them. For any two dereferenceable iterators `i` and `j` such that distance from `i` to `j` is positive, the following condition holds:

```cpp
value_comp(*j, *i) == false
```

**12** For associative containers with unique keys the stronger condition holds:

```cpp
value_comp(*i, *j) != false
```

**13** When an associative container is constructed by passing a comparison object the container shall not store a pointer or reference to the passed object, even if that object is passed by reference. When an associative container is copied, through either a copy constructor or an assignment operator, the target container shall then use the comparison object from the container being copied, as if that comparison object had been passed to the target container in its constructor.

**14** The member function templates `find`, `count`, `contains`, `lower_bound`, `upper_bound`, and `equal_range` shall not participate in overload resolution unless the *qualified-id* `Compare::is_transparent` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)).

**15** A deduction guide for an associative container shall not participate in overload resolution if any of the following are true:

- It has an `InputIterator` template parameter and a type that does not qualify as an input iterator is deduced for that parameter.
- It has an `Allocator` template parameter and a type that does not qualify as an allocator is deduced for that parameter.
- It has a `Compare` template parameter and a type that qualifies as an allocator is deduced for that parameter.

## Subclauses

- [22.2.6.1 Exception safety guarantees \[associative.reqmts.except\]](associative.reqmts.except.md)
