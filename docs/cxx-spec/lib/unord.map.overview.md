# 22.5.4.1 Overview [unord.map.overview]

[Contents](../README.md) · up: [22.5.4 Class template `unordered_map` \[unord.map\]](unord.map.md) · prev: [22.5.4 Class template `unordered_map` \[unord.map\]](unord.map.md) · next: [22.5.4.2 Constructors \[unord.map.cnstr\]](unord.map.cnstr.md)

**1** An `unordered_map` is an unordered associative container that supports unique keys (an `unordered_map` contains at most one of each key value) and that associates values of another type `mapped_type` with the keys. The `unordered_map` class supports forward iterators.

**2** An `unordered_map` meets all of the requirements of a container, of an unordered associative container, and of an allocator-aware container ([Table 76](container.requirements.general.md#tab:container.alloc.req)). It provides the operations described in the preceding requirements table for unique keys; that is, an `unordered_map` supports the `a_uniq` operations in that table, not the `a_eq` operations. For an `unordered_map<Key, T>` the `key type` is `Key`, the mapped type is `T`, and the value type is `pair<const Key, T>`.

**3** Subclause [[unord.map]](unord.map.md) only describes operations on `unordered_map` that are not described in one of the requirement tables, or for which there is additional semantic information.

```cpp
namespace std {
  template<class Key,
           class T,
           class Hash = hash<Key>,
           class Pred = equal_to<Key>,
           class Allocator = allocator<pair<const Key, T>>>
  class unordered_map {
  public:
    // types
    using key_type             = Key;
    using mapped_type          = T;
    using value_type           = pair<const Key, T>;
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

    // [unord.map.cnstr], construct/copy/destroy
    unordered_map();
    explicit unordered_map(size_type n,
                           const hasher& hf = hasher(),
                           const key_equal& eql = key_equal(),
                           const allocator_type& a = allocator_type());
    template<class InputIterator>
      unordered_map(InputIterator f, InputIterator l,
                    size_type n = see below,
                    const hasher& hf = hasher(),
                    const key_equal& eql = key_equal(),
                    const allocator_type& a = allocator_type());
    unordered_map(const unordered_map&);
    unordered_map(unordered_map&&);
    explicit unordered_map(const Allocator&);
    unordered_map(const unordered_map&, const Allocator&);
    unordered_map(unordered_map&&, const Allocator&);
    unordered_map(initializer_list<value_type> il,
                  size_type n = see below,
                  const hasher& hf = hasher(),
                  const key_equal& eql = key_equal(),
                  const allocator_type& a = allocator_type());
    unordered_map(size_type n, const allocator_type& a)
      : unordered_map(n, hasher(), key_equal(), a) { }
    unordered_map(size_type n, const hasher& hf, const allocator_type& a)
      : unordered_map(n, hf, key_equal(), a) { }
    template<class InputIterator>
      unordered_map(InputIterator f, InputIterator l, size_type n, const allocator_type& a)
        : unordered_map(f, l, n, hasher(), key_equal(), a) { }
    template<class InputIterator>
      unordered_map(InputIterator f, InputIterator l, size_type n, const hasher& hf,
                    const allocator_type& a)
        : unordered_map(f, l, n, hf, key_equal(), a) { }
    unordered_map(initializer_list<value_type> il, size_type n, const allocator_type& a)
      : unordered_map(il, n, hasher(), key_equal(), a) { }
    unordered_map(initializer_list<value_type> il, size_type n, const hasher& hf,
                  const allocator_type& a)
      : unordered_map(il, n, hf, key_equal(), a) { }
    ~unordered_map();
    unordered_map& operator=(const unordered_map&);
    unordered_map& operator=(unordered_map&&)
      noexcept(allocator_traits<Allocator>::is_always_equal::value &&
               is_nothrow_move_assignable_v<Hash> &&
               is_nothrow_move_assignable_v<Pred>);
    unordered_map& operator=(initializer_list<value_type>);
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

    // [unord.map.modifiers], modifiers
    template<class... Args> pair<iterator, bool> emplace(Args&&... args);
    template<class... Args> iterator emplace_hint(const_iterator position, Args&&... args);
    pair<iterator, bool> insert(const value_type& obj);
    pair<iterator, bool> insert(value_type&& obj);
    template<class P> pair<iterator, bool> insert(P&& obj);
    iterator       insert(const_iterator hint, const value_type& obj);
    iterator       insert(const_iterator hint, value_type&& obj);
    template<class P> iterator insert(const_iterator hint, P&& obj);
    template<class InputIterator> void insert(InputIterator first, InputIterator last);
    void insert(initializer_list<value_type>);

    node_type extract(const_iterator position);
    node_type extract(const key_type& x);
    insert_return_type insert(node_type&& nh);
    iterator           insert(const_iterator hint, node_type&& nh);

    template<class... Args>
      pair<iterator, bool> try_emplace(const key_type& k, Args&&... args);
    template<class... Args>
      pair<iterator, bool> try_emplace(key_type&& k, Args&&... args);
    template<class... Args>
      iterator try_emplace(const_iterator hint, const key_type& k, Args&&... args);
    template<class... Args>
      iterator try_emplace(const_iterator hint, key_type&& k, Args&&... args);
    template<class M>
      pair<iterator, bool> insert_or_assign(const key_type& k, M&& obj);
    template<class M>
      pair<iterator, bool> insert_or_assign(key_type&& k, M&& obj);
    template<class M>
      iterator insert_or_assign(const_iterator hint, const key_type& k, M&& obj);
    template<class M>
      iterator insert_or_assign(const_iterator hint, key_type&& k, M&& obj);

    iterator  erase(iterator position);
    iterator  erase(const_iterator position);
    size_type erase(const key_type& k);
    iterator  erase(const_iterator first, const_iterator last);
    void      swap(unordered_map&)
      noexcept(allocator_traits<Allocator>::is_always_equal::value &&
               is_nothrow_swappable_v<Hash> &&
               is_nothrow_swappable_v<Pred>);
    void      clear() noexcept;

    template<class H2, class P2>
      void merge(unordered_map<Key, T, H2, P2, Allocator>& source);
    template<class H2, class P2>
      void merge(unordered_map<Key, T, H2, P2, Allocator>&& source);
    template<class H2, class P2>
      void merge(unordered_multimap<Key, T, H2, P2, Allocator>& source);
    template<class H2, class P2>
      void merge(unordered_multimap<Key, T, H2, P2, Allocator>&& source);

    // observers
    hasher hash_function() const;
    key_equal key_eq() const;

    // map operations
    iterator         find(const key_type& k);
    const_iterator   find(const key_type& k) const;
    template<class K>
      iterator       find(const K& k);
    template<class K>
      const_iterator find(const K& k) const;
    template<class K>
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

    // [unord.map.elem], element access
    mapped_type& operator[](const key_type& k);
    mapped_type& operator[](key_type&& k);
    mapped_type& at(const key_type& k);
    const mapped_type& at(const key_type& k) const;

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
           class Hash = hash<iter-key-type<InputIterator>>,
           class Pred = equal_to<iter-key-type<InputIterator>>,
           class Allocator = allocator<iter-to-alloc-type<InputIterator>>>
    unordered_map(InputIterator, InputIterator, typename see below::size_type = see below,
                  Hash = Hash(), Pred = Pred(), Allocator = Allocator())
      -> unordered_map<iter-key-type<InputIterator>, iter-mapped-type<InputIterator>, Hash, Pred,
                       Allocator>;

  template<class Key, class T, class Hash = hash<Key>,
           class Pred = equal_to<Key>, class Allocator = allocator<pair<const Key, T>>>
    unordered_map(initializer_list<pair<Key, T>>,
                  typename see below::size_type = see below, Hash = Hash(),
                  Pred = Pred(), Allocator = Allocator())
      -> unordered_map<Key, T, Hash, Pred, Allocator>;

  template<class InputIterator, class Allocator>
    unordered_map(InputIterator, InputIterator, typename see below::size_type, Allocator)
      -> unordered_map<iter-key-type<InputIterator>, iter-mapped-type<InputIterator>,
                       hash<iter-key-type<InputIterator>>,
                       equal_to<iter-key-type<InputIterator>>, Allocator>;

  template<class InputIterator, class Allocator>
    unordered_map(InputIterator, InputIterator, Allocator)
      -> unordered_map<iter-key-type<InputIterator>, iter-mapped-type<InputIterator>,
                       hash<iter-key-type<InputIterator>>,
                       equal_to<iter-key-type<InputIterator>>, Allocator>;

  template<class InputIterator, class Hash, class Allocator>
    unordered_map(InputIterator, InputIterator, typename see below::size_type, Hash, Allocator)
      -> unordered_map<iter-key-type<InputIterator>, iter-mapped-type<InputIterator>, Hash,
                       equal_to<iter-key-type<InputIterator>>, Allocator>;

  template<class Key, class T, class Allocator>
    unordered_map(initializer_list<pair<Key, T>>, typename see below::size_type,
                  Allocator)
      -> unordered_map<Key, T, hash<Key>, equal_to<Key>, Allocator>;

  template<class Key, class T, class Allocator>
    unordered_map(initializer_list<pair<Key, T>>, Allocator)
      -> unordered_map<Key, T, hash<Key>, equal_to<Key>, Allocator>;

  template<class Key, class T, class Hash, class Allocator>
    unordered_map(initializer_list<pair<Key, T>>, typename see below::size_type, Hash,
                  Allocator)
      -> unordered_map<Key, T, Hash, equal_to<Key>, Allocator>;

  // swap
  template<class Key, class T, class Hash, class Pred, class Alloc>
    void swap(unordered_map<Key, T, Hash, Pred, Alloc>& x,
              unordered_map<Key, T, Hash, Pred, Alloc>& y)
      noexcept(noexcept(x.swap(y)));
}
```

**4** A `size_type` parameter type in an `unordered_map` deduction guide refers to the `size_type` member type of the type deduced by the deduction guide.
