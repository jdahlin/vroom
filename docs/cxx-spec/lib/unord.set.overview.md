# 22.5.6.1 Overview [unord.set.overview]

[Contents](../README.md) · up: [22.5.6 Class template `unordered_set` \[unord.set\]](unord.set.md) · prev: [22.5.6 Class template `unordered_set` \[unord.set\]](unord.set.md) · next: [22.5.6.2 Constructors \[unord.set.cnstr\]](unord.set.cnstr.md)

**1** An `unordered_set` is an unordered associative container that supports unique keys (an `unordered_set` contains at most one of each key value) and in which the elements’ keys are the elements themselves. The `unordered_set` class supports forward iterators.

**2** An `unordered_set` meets all of the requirements of a container, of an unordered associative container, and of an allocator-aware container ([Table 76](container.requirements.general.md#tab:container.alloc.req)). It provides the operations described in the preceding requirements table for unique keys; that is, an `unordered_set` supports the `a_uniq` operations in that table, not the `a_eq` operations. For an `unordered_set<Key>` the `key type` and the value type are both `Key`. The `iterator` and `const_iterator` types are both constant iterator types. It is unspecified whether they are the same type.

**3** Subclause [[unord.set]](unord.set.md) only describes operations on `unordered_set` that are not described in one of the requirement tables, or for which there is additional semantic information.

```cpp
namespace std {
  template<class Key,
           class Hash = hash<Key>,
           class Pred = equal_to<Key>,
           class Allocator = allocator<Key>>
  class unordered_set {
  public:
    // types
    using key_type             = Key;
    using value_type           = Key;
    using hasher               = Hash;
    using key_equal            = Pred;
    using allocator_type       = Allocator;
    using pointer              = typename allocator_traits<Allocator>::pointer;
    using const_pointer        = typename allocator_traits<Allocator>::const_pointer;
    using reference            = value_type&;
    using const_reference      = const value_type&;
    using size_type            = implementation-defined; // see [container.requirements]
    using difference_type      = implementation-defined; // see [container.requirements]

    using iterator             = implementation-defined; // see [container.requirements]
    using const_iterator       = implementation-defined; // see [container.requirements]
    using local_iterator       = implementation-defined; // see [container.requirements]
    using const_local_iterator = implementation-defined; // see [container.requirements]
    using node_type            = unspecified;
    using insert_return_type   = insert-return-type<iterator, node_type>;

    // [unord.set.cnstr], construct/copy/destroy
    unordered_set();
    explicit unordered_set(size_type n,
                           const hasher& hf = hasher(),
                           const key_equal& eql = key_equal(),
                           const allocator_type& a = allocator_type());
    template<class InputIterator>
      unordered_set(InputIterator f, InputIterator l,
                    size_type n = see below,
                    const hasher& hf = hasher(),
                    const key_equal& eql = key_equal(),
                    const allocator_type& a = allocator_type());
    unordered_set(const unordered_set&);
    unordered_set(unordered_set&&);
    explicit unordered_set(const Allocator&);
    unordered_set(const unordered_set&, const Allocator&);
    unordered_set(unordered_set&&, const Allocator&);
    unordered_set(initializer_list<value_type> il,
                  size_type n = see below,
                  const hasher& hf = hasher(),
                  const key_equal& eql = key_equal(),
                  const allocator_type& a = allocator_type());
    unordered_set(size_type n, const allocator_type& a)
      : unordered_set(n, hasher(), key_equal(), a) { }
    unordered_set(size_type n, const hasher& hf, const allocator_type& a)
      : unordered_set(n, hf, key_equal(), a) { }
    template<class InputIterator>
      unordered_set(InputIterator f, InputIterator l, size_type n, const allocator_type& a)
        : unordered_set(f, l, n, hasher(), key_equal(), a) { }
    template<class InputIterator>
      unordered_set(InputIterator f, InputIterator l, size_type n, const hasher& hf,
                    const allocator_type& a)
      : unordered_set(f, l, n, hf, key_equal(), a) { }
    unordered_set(initializer_list<value_type> il, size_type n, const allocator_type& a)
      : unordered_set(il, n, hasher(), key_equal(), a) { }
    unordered_set(initializer_list<value_type> il, size_type n, const hasher& hf,
                  const allocator_type& a)
      : unordered_set(il, n, hf, key_equal(), a) { }
    ~unordered_set();
    unordered_set& operator=(const unordered_set&);
    unordered_set& operator=(unordered_set&&)
      noexcept(allocator_traits<Allocator>::is_always_equal::value &&
               is_nothrow_move_assignable_v<Hash> &&
               is_nothrow_move_assignable_v<Pred>);
    unordered_set& operator=(initializer_list<value_type>);
    allocator_type get_allocator() const noexcept;

    // iterators
    iterator       begin() noexcept;
    const_iterator begin() const noexcept;
    iterator       end() noexcept;
    const_iterator end() const noexcept;
    const_iterator cbegin() const noexcept;
    const_iterator cend() const noexcept;

    // capacity
    [[nodiscard]] bool empty() const noexcept;
    size_type size() const noexcept;
    size_type max_size() const noexcept;

    // modifiers
    template<class... Args> pair<iterator, bool> emplace(Args&&... args);
    template<class... Args> iterator emplace_hint(const_iterator position, Args&&... args);
    pair<iterator, bool> insert(const value_type& obj);
    pair<iterator, bool> insert(value_type&& obj);
    iterator insert(const_iterator hint, const value_type& obj);
    iterator insert(const_iterator hint, value_type&& obj);
    template<class InputIterator> void insert(InputIterator first, InputIterator last);
    void insert(initializer_list<value_type>);

    node_type extract(const_iterator position);
    node_type extract(const key_type& x);
    insert_return_type insert(node_type&& nh);
    iterator           insert(const_iterator hint, node_type&& nh);

    iterator  erase(iterator position);
    iterator  erase(const_iterator position);
    size_type erase(const key_type& k);
    iterator  erase(const_iterator first, const_iterator last);
    void      swap(unordered_set&)
      noexcept(allocator_traits<Allocator>::is_always_equal::value &&
               is_nothrow_swappable_v<Hash> &&
               is_nothrow_swappable_v<Pred>);
    void      clear() noexcept;

    template<class H2, class P2>
      void merge(unordered_set<Key, H2, P2, Allocator>& source);
    template<class H2, class P2>
      void merge(unordered_set<Key, H2, P2, Allocator>&& source);
    template<class H2, class P2>
      void merge(unordered_multiset<Key, H2, P2, Allocator>& source);
    template<class H2, class P2>
      void merge(unordered_multiset<Key, H2, P2, Allocator>&& source);

    // observers
    hasher hash_function() const;
    key_equal key_eq() const;

    // set operations
    iterator         find(const key_type& k);
    const_iterator   find(const key_type& k) const;
    template<class K>
      iterator       find(const K& k);
    template<class K>
      const_iterator find(const K& k) const;
    size_type        count(const key_type& k) const;
    template<class K>
      size_type      count(const K& k) const;
    bool             contains(const key_type& k) const;
    template<class K>
      bool           contains(const K& k) const;
    pair<iterator, iterator>               equal_range(const key_type& k);
    pair<const_iterator, const_iterator>   equal_range(const key_type& k) const;
    template<class K>
      pair<iterator, iterator>             equal_range(const K& k);
    template<class K>
      pair<const_iterator, const_iterator> equal_range(const K& k) const;

    // bucket interface
    size_type bucket_count() const noexcept;
    size_type max_bucket_count() const noexcept;
    size_type bucket_size(size_type n) const;
    size_type bucket(const key_type& k) const;
    local_iterator begin(size_type n);
    const_local_iterator begin(size_type n) const;
    local_iterator end(size_type n);
    const_local_iterator end(size_type n) const;
    const_local_iterator cbegin(size_type n) const;
    const_local_iterator cend(size_type n) const;

    // hash policy
    float load_factor() const noexcept;
    float max_load_factor() const noexcept;
    void max_load_factor(float z);
    void rehash(size_type n);
    void reserve(size_type n);
  };

  template<class InputIterator,
           class Hash = hash<iter-value-type<InputIterator>>,
           class Pred = equal_to<iter-value-type<InputIterator>>,
           class Allocator = allocator<iter-value-type<InputIterator>>>
    unordered_set(InputIterator, InputIterator, typename see below::size_type = see below,
                  Hash = Hash(), Pred = Pred(), Allocator = Allocator())
      -> unordered_set<iter-value-type<InputIterator>,
                       Hash, Pred, Allocator>;

  template<class T, class Hash = hash<T>,
           class Pred = equal_to<T>, class Allocator = allocator<T>>
    unordered_set(initializer_list<T>, typename see below::size_type = see below,
                  Hash = Hash(), Pred = Pred(), Allocator = Allocator())
      -> unordered_set<T, Hash, Pred, Allocator>;

  template<class InputIterator, class Allocator>
    unordered_set(InputIterator, InputIterator, typename see below::size_type, Allocator)
      -> unordered_set<iter-value-type<InputIterator>,
                       hash<iter-value-type<InputIterator>>,
                       equal_to<iter-value-type<InputIterator>>,
                       Allocator>;

  template<class InputIterator, class Hash, class Allocator>
    unordered_set(InputIterator, InputIterator, typename see below::size_type,
                  Hash, Allocator)
      -> unordered_set<iter-value-type<InputIterator>, Hash,
                       equal_to<iter-value-type<InputIterator>>,
                       Allocator>;

  template<class T, class Allocator>
    unordered_set(initializer_list<T>, typename see below::size_type, Allocator)
      -> unordered_set<T, hash<T>, equal_to<T>, Allocator>;

  template<class T, class Hash, class Allocator>
    unordered_set(initializer_list<T>, typename see below::size_type, Hash, Allocator)
      -> unordered_set<T, Hash, equal_to<T>, Allocator>;

  // swap
  template<class Key, class Hash, class Pred, class Alloc>
    void swap(unordered_set<Key, Hash, Pred, Alloc>& x,
              unordered_set<Key, Hash, Pred, Alloc>& y)
      noexcept(noexcept(x.swap(y)));
}
```

**4** A `size_type` parameter type in an `unordered_set` deduction guide refers to the `size_type` member type of the type deduced by the deduction guide.
