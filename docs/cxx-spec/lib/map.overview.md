# 22.4.4.1 Overview [map.overview]

[Contents](../README.md) · up: [22.4.4 Class template `map` \[map\]](map.md) · prev: [22.4.4 Class template `map` \[map\]](map.md) · next: [22.4.4.2 Constructors, copy, and assignment \[map.cons\]](map.cons.md)

**1** A `map` is an associative container that supports unique keys (contains at most one of each key value) and provides for fast retrieval of values of another type `T` based on the keys. The `map` class supports bidirectional iterators.

**2** A `map` meets all of the requirements of a container, of a reversible container ([[container.requirements]](container.requirements.md)), of an associative container ([[associative.reqmts]](associative.reqmts.md)), and of an allocator-aware container ([Table 76](container.requirements.general.md#tab:container.alloc.req)). A `map` also provides most operations described in [[associative.reqmts]](associative.reqmts.md) for unique keys. This means that a `map` supports the `a_uniq` operations in [[associative.reqmts]](associative.reqmts.md) but not the `a_eq` operations. For a `map<Key,T>` the `key_type` is `Key` and the `value_type` is `pair<const Key,T>`. Descriptions are provided here only for operations on `map` that are not described in one of those tables or for operations where there is additional semantic information.

```cpp
namespace std {
  template<class Key, class T, class Compare = less<Key>,
           class Allocator = allocator<pair<const Key, T>>>
  class map {
  public:
    // types
    using key_type               = Key;
    using mapped_type            = T;
    using value_type             = pair<const Key, T>;
    using key_compare            = Compare;
    using allocator_type         = Allocator;
    using pointer                = typename allocator_traits<Allocator>::pointer;
    using const_pointer          = typename allocator_traits<Allocator>::const_pointer;
    using reference              = value_type&;
    using const_reference        = const value_type&;
    using size_type              = implementation-defined; // see [container.requirements]
    using difference_type        = implementation-defined; // see [container.requirements]
    using iterator               = implementation-defined; // see [container.requirements]
    using const_iterator         = implementation-defined; // see [container.requirements]
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using node_type              = unspecified;
    using insert_return_type     = insert-return-type<iterator, node_type>;

    class value_compare {
      friend class map;
    protected:
      Compare comp;
      value_compare(Compare c) : comp(c) {}
    public:
      bool operator()(const value_type& x, const value_type& y) const {
        return comp(x.first, y.first);
      }
    };

    // [map.cons], construct/copy/destroy
    map() : map(Compare()) { }
    explicit map(const Compare& comp, const Allocator& = Allocator());
    template<class InputIterator>
      map(InputIterator first, InputIterator last,
          const Compare& comp = Compare(), const Allocator& = Allocator());
    map(const map& x);
    map(map&& x);
    explicit map(const Allocator&);
    map(const map&, const Allocator&);
    map(map&&, const Allocator&);
    map(initializer_list<value_type>,
      const Compare& = Compare(),
      const Allocator& = Allocator());
    template<class InputIterator>
      map(InputIterator first, InputIterator last, const Allocator& a)
        : map(first, last, Compare(), a) { }
    map(initializer_list<value_type> il, const Allocator& a)
      : map(il, Compare(), a) { }
    ~map();
    map& operator=(const map& x);
    map& operator=(map&& x)
      noexcept(allocator_traits<Allocator>::is_always_equal::value &&
               is_nothrow_move_assignable_v<Compare>);
    map& operator=(initializer_list<value_type>);
    allocator_type get_allocator() const noexcept;

    // iterators
    iterator               begin() noexcept;
    const_iterator         begin() const noexcept;
    iterator               end() noexcept;
    const_iterator         end() const noexcept;

    reverse_iterator       rbegin() noexcept;
    const_reverse_iterator rbegin() const noexcept;
    reverse_iterator       rend() noexcept;
    const_reverse_iterator rend() const noexcept;

    const_iterator         cbegin() const noexcept;
    const_iterator         cend() const noexcept;
    const_reverse_iterator crbegin() const noexcept;
    const_reverse_iterator crend() const noexcept;

    // capacity
    [[nodiscard]] bool empty() const noexcept;
    size_type size() const noexcept;
    size_type max_size() const noexcept;

    // [map.access], element access
    mapped_type& operator[](const key_type& x);
    mapped_type& operator[](key_type&& x);
    mapped_type&       at(const key_type& x);
    const mapped_type& at(const key_type& x) const;

    // [map.modifiers], modifiers
    template<class... Args> pair<iterator, bool> emplace(Args&&... args);
    template<class... Args> iterator emplace_hint(const_iterator position, Args&&... args);
    pair<iterator, bool> insert(const value_type& x);
    pair<iterator, bool> insert(value_type&& x);
    template<class P> pair<iterator, bool> insert(P&& x);
    iterator insert(const_iterator position, const value_type& x);
    iterator insert(const_iterator position, value_type&& x);
    template<class P>
      iterator insert(const_iterator position, P&&);
    template<class InputIterator>
      void insert(InputIterator first, InputIterator last);
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
    size_type erase(const key_type& x);
    iterator  erase(const_iterator first, const_iterator last);
    void      swap(map&)
      noexcept(allocator_traits<Allocator>::is_always_equal::value &&
               is_nothrow_swappable_v<Compare>);
    void      clear() noexcept;

    template<class C2>
      void merge(map<Key, T, C2, Allocator>& source);
    template<class C2>
      void merge(map<Key, T, C2, Allocator>&& source);
    template<class C2>
      void merge(multimap<Key, T, C2, Allocator>& source);
    template<class C2>
      void merge(multimap<Key, T, C2, Allocator>&& source);

    // observers
    key_compare key_comp() const;
    value_compare value_comp() const;

    // map operations
    iterator       find(const key_type& x);
    const_iterator find(const key_type& x) const;
    template<class K> iterator       find(const K& x);
    template<class K> const_iterator find(const K& x) const;

    size_type      count(const key_type& x) const;
    template<class K> size_type count(const K& x) const;

    bool           contains(const key_type& x) const;
    template<class K> bool contains(const K& x) const;

    iterator       lower_bound(const key_type& x);
    const_iterator lower_bound(const key_type& x) const;
    template<class K> iterator       lower_bound(const K& x);
    template<class K> const_iterator lower_bound(const K& x) const;

    iterator       upper_bound(const key_type& x);
    const_iterator upper_bound(const key_type& x) const;
    template<class K> iterator       upper_bound(const K& x);
    template<class K> const_iterator upper_bound(const K& x) const;

    pair<iterator, iterator>               equal_range(const key_type& x);
    pair<const_iterator, const_iterator>   equal_range(const key_type& x) const;
    template<class K>
      pair<iterator, iterator>             equal_range(const K& x);
    template<class K>
      pair<const_iterator, const_iterator> equal_range(const K& x) const;
  };

  template<class InputIterator, class Compare = less<iter-key-type<InputIterator>>,
           class Allocator = allocator<iter-to-alloc-type<InputIterator>>>
    map(InputIterator, InputIterator, Compare = Compare(), Allocator = Allocator())
      -> map<iter-key-type<InputIterator>, iter-mapped-type<InputIterator>, Compare, Allocator>;

  template<class Key, class T, class Compare = less<Key>,
           class Allocator = allocator<pair<const Key, T>>>
    map(initializer_list<pair<Key, T>>, Compare = Compare(), Allocator = Allocator())
      -> map<Key, T, Compare, Allocator>;

  template<class InputIterator, class Allocator>
    map(InputIterator, InputIterator, Allocator)
      -> map<iter-key-type<InputIterator>, iter-mapped-type<InputIterator>,
             less<iter-key-type<InputIterator>>, Allocator>;

  template<class Key, class T, class Allocator>
    map(initializer_list<pair<Key, T>>, Allocator) -> map<Key, T, less<Key>, Allocator>;

  // swap
  template<class Key, class T, class Compare, class Allocator>
    void swap(map<Key, T, Compare, Allocator>& x,
              map<Key, T, Compare, Allocator>& y)
      noexcept(noexcept(x.swap(y)));
}
```
