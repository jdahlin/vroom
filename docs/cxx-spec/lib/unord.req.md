# 22.2.7 Unordered associative containers [unord.req]

[Contents](../README.md) · up: [22.2 Container requirements \[container.requirements\]](container.requirements.md) · prev: [22.2.6.1 Exception safety guarantees \[associative.reqmts.except\]](associative.reqmts.except.md) · next: [22.2.7.1 Exception safety guarantees \[unord.req.except\]](unord.req.except.md)

**1** Unordered associative containers provide an ability for fast retrieval of data based on keys. The worst-case complexity for most operations is linear, but the average case is much faster. The library provides four unordered associative containers: `unordered_set`, `unordered_map`, `unordered_multiset`, and `unordered_multimap`.

**2** Unordered associative containers conform to the requirements for Containers ([[container.requirements]](container.requirements.md)), except that the expressions `a == b` and `a != b` have different semantics than for the other container types.

**3** Each unordered associative container is parameterized by `Key`, by a function object type `Hash` that meets the *Cpp17Hash* requirements ([[hash.requirements]](hash.requirements.md)) and acts as a hash function for argument values of type `Key`, and by a binary predicate `Pred` that induces an equivalence relation on values of type `Key`. Additionally, `unordered_map` and `unordered_multimap` associate an arbitrary *mapped type* `T` with the `Key`.

**4** The container’s object of type `Hash` — denoted by `hash` — is called the *hash function* of the container. The container’s object of type `Pred` — denoted by `pred` — is called the *key equality predicate* of the container.

**5** Two values `k1` and `k2` are considered equivalent if the container’s key equality predicate `pred(k1, k2)` is valid and returns `true` when passed those values. If `k1` and `k2` are equivalent, the container’s hash function shall return the same value for both. [*Note 1*: Thus, when an unordered associative container is instantiated with a non-default `Pred` parameter it usually needs a non-default `Hash` parameter as well. — *end note*] For any two keys `k1` and `k2` in the same container, calling `pred(k1, k2)` shall always return the same value. For any key `k` in a container, calling `hash(k)` shall always return the same value.

**6** An unordered associative container supports *unique keys* if it may contain at most one element for each key. Otherwise, it supports *equivalent keys*. `unordered_set` and `unordered_map` support unique keys. `unordered_multiset` and `unordered_multimap` support equivalent keys. In containers that support equivalent keys, elements with equivalent keys are adjacent to each other in the iteration order of the container. Thus, although the absolute order of elements in an unordered container is not specified, its elements are grouped into *equivalent-key groups* such that all elements of each group have equivalent keys. Mutating operations on unordered containers shall preserve the relative order of elements within each equivalent-key group unless otherwise specified.

**7** For `unordered_set` and `unordered_multiset` the value type is the same as the key type. For `unordered_map` and `unordered_multimap` it is `pair<const Key, T>`.

**8** For unordered containers where the value type is the same as the key type, both `iterator` and `const_iterator` are constant iterators. It is unspecified whether or not `iterator` and `const_iterator` are the same type. [*Note 2*: `iterator` and `const_iterator` have identical semantics in this case, and `iterator` is convertible to `const_iterator`. Users can avoid violating the one-definition rule by always using `const_iterator` in their function parameter lists. — *end note*]

**9** The elements of an unordered associative container are organized into *buckets*. Keys with the same hash code appear in the same bucket. The number of buckets is automatically increased as elements are added to an unordered associative container, so that the average number of elements per bucket is kept below a bound. Rehashing invalidates iterators, changes ordering between elements, and changes which buckets elements appear in, but does not invalidate pointers or references to elements. For `unordered_multiset` and `unordered_multimap`, rehashing preserves the relative ordering of equivalent elements.

**10** The unordered associative containers meet all the requirements of Allocator-aware containers ([[container.requirements.general]](container.requirements.general.md)), except that for `unordered_map` and `unordered_multimap`, the requirements placed on `value_type` in [Table 76](container.requirements.general.md#tab:container.alloc.req) apply instead to `key_type` and `mapped_type`. [*Note 3*: For example, `key_type` and `mapped_type` are sometimes required to be *Cpp17CopyAssignable* even though the associated `value_type`, `pair<const key_type, mapped_type>`, is not *Cpp17CopyAssignable*. — *end note*]

**11** In [Table 81](unord.req.md#tab:container.hash.req):

- `X` denotes an unordered associative container class,

- `a` denotes a value of type `X`,

- `a2` denotes a value of a type with nodes compatible with type `X` ([Table 79](container.node.overview.md#tab:container.node.compat)),

- `b` denotes a possibly const value of type `X`,

- `a_uniq` denotes a value of type `X` when `X` supports unique keys,

- `a_eq` denotes a value of type `X` when `X` supports equivalent keys,

- `a_tran` denotes a possibly const value of type `X` when the *qualified-id*s `X::key_equal::is_transparent` and `X::hasher::is_transparent` are both valid and denote types ([[temp.deduct]](../temp.deduct.md)),

- `i` and `j` denote input iterators that refer to `value_type`,

- `[i, j)` denotes a valid range,

- `p` and `q2` denote valid constant iterators to `a`,

- `q` and `q1` denote valid dereferenceable constant iterators to `a`,

- `r` denotes a valid dereferenceable iterator to `a`,

- `[q1, q2)` denotes a valid range in `a`,

- `il` denotes a value of type `initializer_list<value_type>`,

- `t` denotes a value of type `X::value_type`,

- `k` denotes a value of type `key_type`,

- `hf` denotes a possibly const value of type `hasher`,

- `eq` denotes a possibly const value of type `key_equal`,

- `ke` is a value such that

  - `eq(r1, ke) == eq(ke, r1)`
  - `hf(r1) == hf(ke)` if `eq(r1, ke)` is `true`, and
  - `(eq(r1, ke) && eq(r1, r2)) == eq(r2, ke)`

  where `r1` and `r2` are keys of elements in `a_tran`,

- `n` denotes a value of type `size_type`,

- `z` denotes a value of type `float`, and

- `nh` denotes a non-const rvalue of type `X::node_type`.

<a id="tab:container.hash.req"></a>
**Table 81: Unordered associative container requirements (in addition to container)** [tab:container.hash.req]

| Expression | Return type | Assertion/note | Complexity |
| --- | --- | --- | --- |
|  |  | pre-/post-condition |  |
| `X::key_type` | `Key` |  | compile time |
| `X::mapped_type` (`unordered_map` and `unordered_multimap` only) | `T` |  | compile time |
| `X::value_type` (`unordered_set` and `unordered_multiset` only) | `Key` | *Preconditions:* `value_type` is *Cpp17Erasable* from `X` | compile time |
| `X::value_type` (`unordered_map` and `unordered_multimap` only) | `pair<const Key, T>` | *Preconditions:* `value_type` is *Cpp17Erasable* from `X` | compile time |
| `X::hasher` | `Hash` | *Preconditions:* `Hash` is a unary function object type such that the expression `hf(k)` has type `size_t`. | compile time |
| `X::key_equal` | `Pred` | *Preconditions:* `Pred` meets the *Cpp17CopyConstructible* requirements.<br>`Pred` is a binary predicate that takes two arguments of type `Key`. `Pred` is an equivalence relation. | compile time |
| `X::local_iterator` | An iterator type whose category, value type, difference type, and pointer and reference types are the same as `X::iterator`’s. | A `local_iterator` object may be used to iterate through a single bucket, but may not be used to iterate across buckets. | compile time |
| `X::const_local_iterator` | An iterator type whose category, value type, difference type, and pointer and reference types are the same as `X::const_iterator`’s. | A `const_local_iterator` object may be used to iterate through a single bucket, but may not be used to iterate across buckets. | compile time |
| `X::node_type` | a specialization of a `node-handle` class template, such that the public nested types are the same types as the corresponding types in `X`. | see [[container.node]](container.node.md) | compile time |
| `X(n, hf, eq)`<br>`X a(n, hf, eq);` | `X` | *Effects:* Constructs an empty container with at least `n` buckets, using `hf` as the hash function and `eq` as the key equality predicate. | 𝑂(`n`) |
| `X(n, hf)`<br>`X a(n, hf);` | `X` | *Preconditions:* `key_equal` meets the *Cpp17DefaultConstructible* requirements.<br>*Effects:* Constructs an empty container with at least `n` buckets, using `hf` as the hash function and `key_equal()` as the key equality predicate. | 𝑂(`n`) |
| `X(n)`<br>`X a(n);` | `X` | *Preconditions:* `hasher` and `key_equal` meet the *Cpp17DefaultConstructible* requirements.<br>*Effects:* Constructs an empty container with at least `n` buckets, using `hasher()` as the hash function and `key_equal()` as the key equality predicate. | 𝑂(`n`) |
| `X()`<br>`X a;` | `X` | *Preconditions:* `hasher` and `key_equal` meet the *Cpp17DefaultConstructible* requirements.<br>*Effects:* Constructs an empty container with an unspecified number of buckets, using `hasher()` as the hash function and `key_equal()` as the key equality predicate. | constant |
| `X(i, j, n, hf, eq)`<br>`X a(i, j, n, hf, eq);` | `X` | *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `X` from `*i`.<br>*Effects:* Constructs an empty container with at least `n` buckets, using `hf` as the hash function and `eq` as the key equality predicate, and inserts elements from `[i, j)` into it. | Average case 𝑂(*N*) (*N* is `distance(i, j)`), worst case 𝑂(*N*<sup>2</sup>) |
| `X(i, j, n, hf)`<br>`X a(i, j, n, hf);` | `X` | *Preconditions:* `key_equal` meets the *Cpp17DefaultConstructible* requirements. `value_type` is *Cpp17EmplaceConstructible* into `X` from `*i`.<br>*Effects:* Constructs an empty container with at least `n` buckets, using `hf` as the hash function and `key_equal()` as the key equality predicate, and inserts elements from `[i, j)` into it. | Average case 𝑂(*N*) (*N* is `distance(i, j)`), worst case 𝑂(*N*<sup>2</sup>) |
| `X(i, j, n)`<br>`X a(i, j, n);` | `X` | *Preconditions:* `hasher` and `key_equal` meet the *Cpp17DefaultConstructible* requirements. `value_type` is *Cpp17EmplaceConstructible* into `X` from `*i`.<br>*Effects:* Constructs an empty container with at least `n` buckets, using `hasher()` as the hash function and `key_equal()` as the key equality predicate, and inserts elements from `[i, j)` into it. | Average case 𝑂(*N*) (*N* is `distance(i, j)`), worst case 𝑂(*N*<sup>2</sup>) |
| `X(i, j)`<br>`X a(i, j);` | `X` | *Preconditions:* `hasher` and `key_equal` meet the *Cpp17DefaultConstructible* requirements. `value_type` is *Cpp17EmplaceConstructible* into `X` from `*i`.<br>*Effects:* Constructs an empty container with an unspecified number of buckets, using `hasher()` as the hash function and `key_equal()` as the key equality predicate, and inserts elements from `[i, j)` into it. | Average case 𝑂(*N*) (*N* is `distance(i, j)`), worst case 𝑂(*N*<sup>2</sup>) |
| `X(il)` | `X` | Same as `X(il.begin(), il.end())`. | Same as `X(il.begin(),` `il.end())`. |
| `X(il, n)` | `X` | Same as `X(il.begin(), il.end(), n)`. | Same as `X(il.begin(),` `il.end(), n)`. |
| `X(il, n, hf)` | `X` | Same as `X(il.begin(), il.end(), n, hf)`. | Same as `X(il.begin(),` `il.end(), n, hf)`. |
| `X(il, n, hf, eq)` | `X` | Same as `X(il.begin(), il.end(), n, hf, eq)`. | Same as `X(il.begin(),` `il.end(), n, hf, eq)`. |
| `X(b)`<br>`X a(b);` | `X` | Copy constructor. In addition to the requirements of [Table 73](container.requirements.general.md#tab:container.req), copies the hash function, predicate, and maximum load factor. | Average case linear in `b.size()`, worst case quadratic. |
| `a = b` | `X&` | Copy assignment operator. In addition to the requirements of [Table 73](container.requirements.general.md#tab:container.req), copies the hash function, predicate, and maximum load factor. | Average case linear in `b.size()`, worst case quadratic. |
| `a = il` | `X&` | *Preconditions:* `value_type` is *Cpp17CopyInsertable* into `X` and *Cpp17CopyAssignable*.<br>*Effects:* Assigns the range `[il.begin(), il.end())` into `a`. All existing elements of `a` are either assigned to or destroyed. | Same as `a = X(il)`. |
| `b.hash_function()` | `hasher` | *Returns:* `b`’s hash function. | constant |
| `b.key_eq()` | `key_equal` | *Returns:* `b`’s key equality predicate. | constant |
| `a_uniq.` `emplace(args)` | `pair<iterator,` `bool>` | *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `X` from `args`.<br>*Effects:* Inserts a `value_type` object `t` constructed with `std::forward<Args>(args)...` if and only if there is no element in the container with key equivalent to the key of `t`. The `bool` component of the returned pair is `true` if and only if the insertion takes place, and the iterator component of the pair points to the element with key equivalent to the key of `t`. | Average case 𝑂(1), worst case 𝑂(`a_uniq.`<br>`size()`). |
| `a_eq.``emplace(args)` | `iterator` | *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `X` from `args`.<br>*Effects:* Inserts a `value_type` object `t` constructed with `std::forward<Args>(args)...` and returns the iterator pointing to the newly inserted element. | Average case 𝑂(1), worst case 𝑂(`a_eq.`<br>`size()`). |
| `a.emplace_hint(p, args)` | `iterator` | *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `X` from `args`.<br>*Effects:* Equivalent to `a.emplace(` `std::forward<Args>(args)...)`. Return value is an iterator pointing to the element with the key equivalent to the newly inserted element. The `const_iterator` `p` is a hint pointing to where the search should start. Implementations are permitted to ignore the hint. | Average case 𝑂(1), worst case 𝑂(`a.` `size()`). |
| `a_uniq.insert(t)` | `pair<iterator, bool>` | *Preconditions:* If `t` is a non-const rvalue, `value_type` is *Cpp17MoveInsertable* into `X`; otherwise, `value_type` is *Cpp17CopyInsertable* into `X`.<br>*Effects:* Inserts `t` if and only if there is no element in the container with key equivalent to the key of `t`. The `bool` component of the returned pair indicates whether the insertion takes place, and the `iterator` component points to the element with key equivalent to the key of `t`. | Average case 𝑂(1), worst case 𝑂(`a_uniq.`<br>`size()`). |
| `a_eq.insert(t)` | `iterator` | *Preconditions:* If `t` is a non-const rvalue, `value_type` is *Cpp17MoveInsertable* into `X`; otherwise, `value_type` is *Cpp17CopyInsertable* into `X`.<br>*Effects:* Inserts `t`, and returns an iterator pointing to the newly inserted element. | Average case 𝑂(1), worst case 𝑂(`a_eq.`<br>`size()`). |
| `a.insert(p, t)` | `iterator` | *Preconditions:* If `t` is a non-const rvalue, `value_type` is *Cpp17MoveInsertable* into `X`; otherwise, `value_type` is *Cpp17CopyInsertable* into `X`.<br>*Effects:* Equivalent to `a.insert(t)`. Return value is an iterator pointing to the element with the key equivalent to that of `t`. The iterator `p` is a hint pointing to where the search should start. Implementations are permitted to ignore the hint. | Average case 𝑂(1), worst case 𝑂(`a.size()`). |
| `a.insert(i, j)` | `void` | *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `X` from `*i`. Neither `i` nor `j` are iterators into `a`.<br>*Effects:* Equivalent to `a.insert(t)` for each element in `[i,j)`. | Average case 𝑂(*N*), where *N* is `distance(i, j)`, worst case 𝑂(*N*(`a.size()`+1)). |
| `a.insert(il)` | `void` | Same as `a.insert(il.begin(), il.end())`. | Same as `a.insert(` `il.begin(),` `il.end())`. |
| `a_uniq.`<br>`insert(nh)` | `insert_return_type` | *Preconditions:* `nh` is empty or `a_uniq.get_allocator() == nh.get_allocator()`.<br>*Effects:* If `nh` is empty, has no effect. Otherwise, inserts the element owned by `nh` if and only if there is no element in the container with a key equivalent to `nh.key()`.<br>*Postconditions:* If `nh` is empty, `inserted` is `false`, `position` is `end()`, and `node` is empty. Otherwise if the insertion took place, `inserted` is `true`, `position` points to the inserted element, and `node` is empty; if the insertion failed, `inserted` is `false`, `node` has the previous value of `nh`, and `position` points to an element with a key equivalent to `nh.key()`. | Average case 𝑂(1), worst case 𝑂(`a_uniq.``size()`). |
| `a_eq.`<br>`insert(nh)` | `iterator` | *Preconditions:* `nh` is empty or `a_eq.get_allocator() == nh.get_allocator()`.<br>*Effects:* If `nh` is empty, has no effect and returns `a_eq.end()`. Otherwise, inserts the element owned by `nh` and returns an iterator pointing to the newly inserted element.<br>*Postconditions:* `nh` is empty. | Average case 𝑂(1), worst case 𝑂(`a_eq.``size()`). |
| `a.insert(q, nh)` | `iterator` | *Preconditions:* `nh` is empty or `a.get_allocator() == nh.get_allocator()`.<br>*Effects:* If `nh` is empty, has no effect and returns `a.end()`. Otherwise, inserts the element owned by `nh` if and only if there is no element with key equivalent to `nh.key()` in containers with unique keys; always inserts the element owned by `nh` in containers with equivalent keys. Always returns the iterator pointing to the element with key equivalent to `nh.key()`. The iterator `q` is a hint pointing to where the search should start. Implementations are permitted to ignore the hint.<br>*Postconditions:* `nh` is empty if insertion succeeds, unchanged if insertion fails. | Average case 𝑂(1), worst case 𝑂(`a.size()`). |
| `a.extract(k)` | `node_type` | *Effects:* Removes an element in the container with key equivalent to `k`.<br>*Returns:* A `node_type` owning the element if found, otherwise an empty `node_type`. | Average case 𝑂(1), worst case 𝑂(`a.size()`). |
| `a.extract(q)` | `node_type` | *Effects:* Removes the element pointed to by `q`.<br>*Returns:* A `node_type` owning that element. | Average case 𝑂(1), worst case 𝑂(`a.size()`). |
| `a.merge(a2)` | `void` | *Preconditions:* `a.get_allocator() == a2.get_allocator()`.<br>Attempts to extract each element in `a2` and insert it into `a` using the hash function and key equality predicate of `a`. In containers with unique keys, if there is an element in `a` with key equivalent to the key of an element from `a2`, then that element is not extracted from `a2`.<br>*Postconditions:* Pointers and references to the transferred elements of `a2` refer to those same elements but as members of `a`. Iterators referring to the transferred elements and all iterators referring to `a` will be invalidated, but iterators to elements remaining in `a2` will remain valid. | Average case 𝑂(*N*), where *N* is `a2.size()`, worst case 𝑂(*N*`*a.size()`<br>`+` *N*). |
| `a.erase(k)` | `size_type` | *Effects:* Erases all elements with key equivalent to `k`.<br>*Returns:* The number of elements erased. | Average case 𝑂(`a.count(k)`), worst case 𝑂(`a.size()`). |
| `a.erase(q)` | `iterator` | *Effects:* Erases the element pointed to by `q`.<br>*Returns:* The iterator immediately following `q` prior to the erasure. | Average case 𝑂(1), worst case 𝑂(`a.size()`). |
| `a.erase(r)` | `iterator` | *Effects:* Erases the element pointed to by `r`.<br>*Returns:* The iterator immediately following `r` prior to the erasure. | Average case 𝑂(1), worst case 𝑂(`a.size()`). |
| `a.erase(q1, q2)` | `iterator` | *Effects:* Erases all elements in the range `[q1, q2)`.<br>*Returns:* The iterator immediately following the erased elements prior to the erasure. | Average case linear in `distance(q1, q2)`, worst case 𝑂(`a.size()`). |
| `a.clear()` | `void` | *Effects:* Erases all elements in the container. *Postconditions:* `a.empty()` is `true` | Linear in `a.size()`. |
| `b.find(k)` | `iterator`; <br>`const_iterator` for const `b`. | *Returns:* An iterator pointing to an element with key equivalent to `k`, or `b.end()` if no such element exists. | Average case 𝑂(1), worst case 𝑂(`b.size()`). |
| `a_tran.find(ke)` | `iterator`; <br>`const_iterator` for const `a_tran`. | *Returns:* An iterator pointing to an element with key equivalent to `ke`, or `a_tran.end()` if no such element exists. | Average case 𝑂(1), worst case 𝑂(`a_tran.`<br>`size()`). |
| `b.count(k)` | `size_type` | *Returns:* The number of elements with key equivalent to `k`. | Average case 𝑂(`b.count(k)`), worst case 𝑂(`b.size()`). |
| `a_tran.count(ke)` | `size_type` | *Returns:* The number of elements with key equivalent to `ke`. | Average case 𝑂(`a_tran.`<br>`count(ke)`), worst case 𝑂(`a_tran.`<br>`size()`). |
| `b.contains(k)` | `bool` | *Effects:* Equivalent to `b.find(k) != b.end()` | Average case 𝑂(1), worst case 𝑂(`b.size()`). |
| `a_tran.contains(ke)` | `bool` | *Effects:* Equivalent to `a_tran.find(ke) != a_tran.end()` | Average case 𝑂(1), worst case 𝑂(`a_tran.`<br>`size()`). |
| `b.equal_range(k)` | `pair<iterator, iterator>`; <br>`pair<const_iterator, const_iterator>` for const `b`. | *Returns:* A range containing all elements with keys equivalent to `k`. Returns `make_pair(b.end(), b.end())` if no such elements exist. | Average case 𝑂(`b.count(k)`), worst case 𝑂(`b.size()`). |
| `a_tran.equal_range(ke)` | `pair<iterator, iterator>`; <br>`pair<const_iterator, const_iterator>` for const `a_tran`. | *Returns:* A range containing all elements with keys equivalent to `ke`. Returns `make_pair(a_tran.end(), a_tran.end())` if no such elements exist. | Average case 𝑂(`a_tran.`<br>`count(ke)`), worst case 𝑂(`a_tran.`<br>`size()`). |
| `b.bucket_count()` | `size_type` | *Returns:* The number of buckets that `b` contains. | Constant |
| `b.max_bucket_count()` | `size_type` | *Returns:* An upper bound on the number of buckets that `b` might ever contain. | Constant |
| `b.bucket(k)` | `size_type` | *Preconditions:* `b.bucket_count() > 0`.<br>*Returns:* The index of the bucket in which elements with keys equivalent to `k` would be found, if any such element existed. *Postconditions:* The return value shall be in the range `[0, b.bucket_count())`. | Constant |
| `b.bucket_size(n)` | `size_type` | *Preconditions:* `n` shall be in the range `[0, b.bucket_count())`. *Returns:* The number of elements in the `n`<sup></sup>*th* bucket. | 𝑂(`b.bucket_``size(n)`) |
| `b.begin(n)` | `local_iterator`; <br>`const_local_iterator` for const `b`. | *Preconditions:* `n` is in the range `[0, b.bucket_count())`.<br>*Returns:* An iterator referring to the first element in the bucket. If the bucket is empty, then `b.begin(n) == b.end(n)`. | Constant |
| `b.end(n)` | `local_iterator`; <br>`const_local_iterator` for const `b`. | *Preconditions:* `n` is in the range `[0, b.bucket_count())`.<br>*Returns:* An iterator which is the past-the-end value for the bucket. | Constant |
| `b.cbegin(n)` | `const_local_iterator` | *Preconditions:* `n` shall be in the range `[0, b.bucket_count())`.<br>*Returns:* An iterator referring to the first element in the bucket. If the bucket is empty, then `b.cbegin(n) == b.cend(n)`. | Constant |
| `b.cend(n)` | `const_local_iterator` | *Preconditions:* `n` is in the range `[0, b.bucket_count())`.*Returns:* An iterator which is the past-the-end value for the bucket. | Constant |
| `b.load_factor()` | `float` | *Returns:* The average number of elements per bucket. | Constant |
| `b.max_load_factor()` | `float` | *Returns:* A positive number that the container attempts to keep the load factor less than or equal to. The container automatically increases the number of buckets as necessary to keep the load factor below this number. | Constant |
| `a.max_load_factor(z)` | `void` | *Preconditions:* `z` is positive. May change the container’s maximum load factor, using `z` as a hint. | Constant |
| `a.rehash(n)` | `void` | *Postconditions:* `a.bucket_count() >= a.size() / a.max_load_factor()` and `a.bucket_count() >= n`. | Average case linear in `a.size()`, worst case quadratic. |
| `a.reserve(n)` | `void` | Same as `a.rehash(ceil(n /` `a.max_load_factor()))`. | Average case linear in `a.size()`, worst case quadratic. |

**12** Two unordered containers `a` and `b` compare equal if `a.size() == b.size()` and, for every equivalent-key group `[Ea1, Ea2)` obtained from `a.equal_range(Ea1)`, there exists an equivalent-key group `[Eb1, Eb2)` obtained from `b.equal_range(Ea1)`, such that `is_permutation(Ea1, Ea2, Eb1, Eb2)` returns `true`. For `unordered_set` and `unordered_map`, the complexity of `operator==` (i.e., the number of calls to the `==` operator of the `value_type`, to the predicate returned by `key_eq()`, and to the hasher returned by `hash_function()`) is proportional to *N* in the average case and to *N*<sup>2</sup> in the worst case, where *N* is `a.size()`. For `unordered_multiset` and `unordered_multimap`, the complexity of `operator==` is proportional to $\sum E_i^2$ in the average case and to *N*<sup>2</sup> in the worst case, where *N* is `a.size()`, and *E*<sub>*i*</sub> is the size of the *i*<sup></sup>*th* equivalent-key group in `a`. However, if the respective elements of each corresponding pair of equivalent-key groups *Ea*<sub>*i*</sub> and *Eb*<sub>*i*</sub> are arranged in the same order (as is commonly the case, e.g., if `a` and `b` are unmodified copies of the same container), then the average-case complexity for `unordered_multiset` and `unordered_multimap` becomes proportional to *N* (but worst-case complexity remains 𝑂(*N*<sup>2</sup>), e.g., for a pathologically bad hash function). The behavior of a program that uses `operator==` or `operator!=` on unordered containers is undefined unless the `Pred` function object has the same behavior for both containers and the equality comparison function for `Key` is a refinement[^1] of the partition into equivalent-key groups produced by `Pred`.

**13** The iterator types `iterator` and `const_iterator` of an unordered associative container are of at least the forward iterator category. For unordered associative containers where the key type and value type are the same, both `iterator` and `const_iterator` are constant iterators.

**14** The `insert` and `emplace` members shall not affect the validity of references to container elements, but may invalidate all iterators to the container. The `erase` members shall invalidate only iterators and references to the erased elements, and preserve the relative order of the elements that are not erased.

**15** The `insert` and `emplace` members shall not affect the validity of iterators if `(N+n) <= z * B`, where `N` is the number of elements in the container prior to the insert operation, `n` is the number of elements inserted, `B` is the container’s bucket count, and `z` is the container’s maximum load factor.

**16** The `extract` members invalidate only iterators to the removed element, and preserve the relative order of the elements that are not erased; pointers and references to the removed element remain valid. However, accessing the element through such pointers and references while the element is owned by a `node_type` is undefined behavior. References and pointers to an element obtained while it is owned by a `node_type` are invalidated if the element is successfully inserted.

**17** The member function templates `find`, `count`, `equal_range`, and `contains` shall not participate in overload resolution unless the *qualified-id*s `Pred::is_transparent` and `Hash::is_transparent` are both valid and denote types ([[temp.deduct]](../temp.deduct.md)).

**18** A deduction guide for an unordered associative container shall not participate in overload resolution if any of the following are true:

- It has an `InputIterator` template parameter and a type that does not qualify as an input iterator is deduced for that parameter.
- It has an `Allocator` template parameter and a type that does not qualify as an allocator is deduced for that parameter.
- It has a `Hash` template parameter and an integral type or a type that qualifies as an allocator is deduced for that parameter.
- It has a `Pred` template parameter and a type that qualifies as an allocator is deduced for that parameter.

## Subclauses

- [22.2.7.1 Exception safety guarantees \[unord.req.except\]](unord.req.except.md)

[^1]: Equality comparison is a refinement of partitioning if no two objects that compare equal fall into different partitions.
