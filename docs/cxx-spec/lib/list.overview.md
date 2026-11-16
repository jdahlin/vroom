# 22.3.10.1 Overview [list.overview]

[Contents](../README.md) · up: [22.3.10 Class template `list` \[list\]](list.md) · prev: [22.3.10 Class template `list` \[list\]](list.md) · next: [22.3.10.2 Constructors, copy, and assignment \[list.cons\]](list.cons.md)

**1** A `list` is a sequence container that supports bidirectional iterators and allows constant time insert and erase operations anywhere within the sequence, with storage management handled automatically. Unlike vectors ([[vector]](vector.md)) and deques ([[deque]](deque.md)), fast random access to list elements is not supported, but many algorithms only need sequential access anyway.

**2** A `list` meets all of the requirements of a container, of a reversible container (given in two tables in [[container.requirements]](container.requirements.md)), of a sequence container, including most of the optional sequence container requirements ([[sequence.reqmts]](sequence.reqmts.md)), and of an allocator-aware container ([Table 76](container.requirements.general.md#tab:container.alloc.req)). The exceptions are the `operator[]` and `at` member functions, which are not provided.[^1] Descriptions are provided here only for operations on `list` that are not described in one of these tables or for operations where there is additional semantic information.

```cpp
namespace std {
  template<class T, class Allocator = allocator<T>>
  class list {
  public:
    // types
    using value_type             = T;
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

    // [list.cons], construct/copy/destroy
    list() : list(Allocator()) { }
    explicit list(const Allocator&);
    explicit list(size_type n, const Allocator& = Allocator());
    list(size_type n, const T& value, const Allocator& = Allocator());
    template<class InputIterator>
      list(InputIterator first, InputIterator last, const Allocator& = Allocator());
    list(const list& x);
    list(list&& x);
    list(const list&, const Allocator&);
    list(list&&, const Allocator&);
    list(initializer_list<T>, const Allocator& = Allocator());
    ~list();
    list& operator=(const list& x);
    list& operator=(list&& x)
      noexcept(allocator_traits<Allocator>::is_always_equal::value);
    list& operator=(initializer_list<T>);
    template<class InputIterator>
      void assign(InputIterator first, InputIterator last);
    void assign(size_type n, const T& t);
    void assign(initializer_list<T>);
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

    // [list.capacity], capacity
    [[nodiscard]] bool empty() const noexcept;
    size_type size() const noexcept;
    size_type max_size() const noexcept;
    void      resize(size_type sz);
    void      resize(size_type sz, const T& c);

    // element access
    reference       front();
    const_reference front() const;
    reference       back();
    const_reference back() const;

    // [list.modifiers], modifiers
    template<class... Args> reference emplace_front(Args&&... args);
    template<class... Args> reference emplace_back(Args&&... args);
    void push_front(const T& x);
    void push_front(T&& x);
    void pop_front();
    void push_back(const T& x);
    void push_back(T&& x);
    void pop_back();

    template<class... Args> iterator emplace(const_iterator position, Args&&... args);
    iterator insert(const_iterator position, const T& x);
    iterator insert(const_iterator position, T&& x);
    iterator insert(const_iterator position, size_type n, const T& x);
    template<class InputIterator>
      iterator insert(const_iterator position, InputIterator first, InputIterator last);
    iterator insert(const_iterator position, initializer_list<T> il);

    iterator erase(const_iterator position);
    iterator erase(const_iterator position, const_iterator last);
    void     swap(list&) noexcept(allocator_traits<Allocator>::is_always_equal::value);
    void     clear() noexcept;

    // [list.ops], list operations
    void splice(const_iterator position, list& x);
    void splice(const_iterator position, list&& x);
    void splice(const_iterator position, list& x, const_iterator i);
    void splice(const_iterator position, list&& x, const_iterator i);
    void splice(const_iterator position, list& x, const_iterator first, const_iterator last);
    void splice(const_iterator position, list&& x, const_iterator first, const_iterator last);

    size_type remove(const T& value);
    template<class Predicate> size_type remove_if(Predicate pred);

    size_type unique();
    template<class BinaryPredicate>
      size_type unique(BinaryPredicate binary_pred);

    void merge(list& x);
    void merge(list&& x);
    template<class Compare> void merge(list& x, Compare comp);
    template<class Compare> void merge(list&& x, Compare comp);

    void sort();
    template<class Compare> void sort(Compare comp);

    void reverse() noexcept;
  };

  template<class InputIterator, class Allocator = allocator<iter-value-type<InputIterator>>>
    list(InputIterator, InputIterator, Allocator = Allocator())
      -> list<iter-value-type<InputIterator>, Allocator>;

  // swap
  template<class T, class Allocator>
    void swap(list<T, Allocator>& x, list<T, Allocator>& y)
      noexcept(noexcept(x.swap(y)));
}
```

**3** An incomplete type `T` may be used when instantiating `list` if the allocator meets the allocator completeness requirements ([[allocator.requirements.completeness]](allocator.requirements.completeness.md)). `T` shall be complete before any member of the resulting specialization of `list` is referenced.

[^1]: These member functions are only provided by containers whose iterators are random access iterators.
