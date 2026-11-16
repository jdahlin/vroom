# 22.3.9.1 Overview [forwardlist.overview]

[Contents](../../README.md) · up: [22.3.9 Class template `forward_list` \[forwardlist\]](forwardlist.md) · prev: [22.3.9 Class template `forward_list` \[forwardlist\]](forwardlist.md) · next: [22.3.9.2 Constructors, copy, and assignment \[forwardlist.cons\]](forwardlist.cons.md)

**1** A `forward_list` is a container that supports forward iterators and allows constant time insert and erase operations anywhere within the sequence, with storage management handled automatically. Fast random access to list elements is not supported. [*Note 1*: It is intended that `forward_list` have zero space or time overhead relative to a hand-written C-style singly linked list. Features that would conflict with that goal have been omitted. — *end note*]

**2** A `forward_list` meets all of the requirements of a container ([Table 73](container.requirements.general.md#tab:container.req)), except that the `size()` member function is not provided and `operator==` has linear complexity. A `forward_list` also meets all of the requirements for an allocator-aware container ([Table 76](container.requirements.general.md#tab:container.alloc.req)). In addition, a `forward_list` provides the `assign` member functions ([Table 77](sequence.reqmts.md#tab:container.seq.req)) and several of the optional container requirements ([Table 78](sequence.reqmts.md#tab:container.seq.opt)). Descriptions are provided here only for operations on `forward_list` that are not described in that table or for operations where there is additional semantic information.

**3** [*Note 2*: Modifying any list requires access to the element preceding the first element of interest, but in a `forward_list` there is no constant-time way to access a preceding element. For this reason, ranges that are modified, such as those supplied to `erase` and `splice`, must be open at the beginning. — *end note*]

```cpp
namespace std {
  template<class T, class Allocator = allocator<T>>
  class forward_list {
  public:
    // types
    using value_type      = T;
    using allocator_type  = Allocator;
    using pointer         = typename allocator_traits<Allocator>::pointer;
    using const_pointer   = typename allocator_traits<Allocator>::const_pointer;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using size_type       = implementation-defined; // see [container.requirements]
    using difference_type = implementation-defined; // see [container.requirements]
    using iterator        = implementation-defined; // see [container.requirements]
    using const_iterator  = implementation-defined; // see [container.requirements]

    // [forwardlist.cons], construct/copy/destroy
    forward_list() : forward_list(Allocator()) { }
    explicit forward_list(const Allocator&);
    explicit forward_list(size_type n, const Allocator& = Allocator());
    forward_list(size_type n, const T& value, const Allocator& = Allocator());
    template<class InputIterator>
      forward_list(InputIterator first, InputIterator last, const Allocator& = Allocator());
    forward_list(const forward_list& x);
    forward_list(forward_list&& x);
    forward_list(const forward_list& x, const Allocator&);
    forward_list(forward_list&& x, const Allocator&);
    forward_list(initializer_list<T>, const Allocator& = Allocator());
    ~forward_list();
    forward_list& operator=(const forward_list& x);
    forward_list& operator=(forward_list&& x)
      noexcept(allocator_traits<Allocator>::is_always_equal::value);
    forward_list& operator=(initializer_list<T>);
    template<class InputIterator>
      void assign(InputIterator first, InputIterator last);
    void assign(size_type n, const T& t);
    void assign(initializer_list<T>);
    allocator_type get_allocator() const noexcept;

    // [forwardlist.iter], iterators
    iterator before_begin() noexcept;
    const_iterator before_begin() const noexcept;
    iterator begin() noexcept;
    const_iterator begin() const noexcept;
    iterator end() noexcept;
    const_iterator end() const noexcept;

    const_iterator cbegin() const noexcept;
    const_iterator cbefore_begin() const noexcept;
    const_iterator cend() const noexcept;

    // capacity
    [[nodiscard]] bool empty() const noexcept;
    size_type max_size() const noexcept;

    // [forwardlist.access], element access
    reference front();
    const_reference front() const;

    // [forwardlist.modifiers], modifiers
    template<class... Args> reference emplace_front(Args&&... args);
    void push_front(const T& x);
    void push_front(T&& x);
    void pop_front();

    template<class... Args> iterator emplace_after(const_iterator position, Args&&... args);
    iterator insert_after(const_iterator position, const T& x);
    iterator insert_after(const_iterator position, T&& x);

    iterator insert_after(const_iterator position, size_type n, const T& x);
    template<class InputIterator>
      iterator insert_after(const_iterator position, InputIterator first, InputIterator last);
    iterator insert_after(const_iterator position, initializer_list<T> il);

    iterator erase_after(const_iterator position);
    iterator erase_after(const_iterator position, const_iterator last);
    void swap(forward_list&)
      noexcept(allocator_traits<Allocator>::is_always_equal::value);

    void resize(size_type sz);
    void resize(size_type sz, const value_type& c);
    void clear() noexcept;

    // [forwardlist.ops], forward_list operations
    void splice_after(const_iterator position, forward_list& x);
    void splice_after(const_iterator position, forward_list&& x);
    void splice_after(const_iterator position, forward_list& x, const_iterator i);
    void splice_after(const_iterator position, forward_list&& x, const_iterator i);
    void splice_after(const_iterator position, forward_list& x,
                      const_iterator first, const_iterator last);
    void splice_after(const_iterator position, forward_list&& x,
                      const_iterator first, const_iterator last);

    size_type remove(const T& value);
    template<class Predicate> size_type remove_if(Predicate pred);

    size_type unique();
    template<class BinaryPredicate> size_type unique(BinaryPredicate binary_pred);

    void merge(forward_list& x);
    void merge(forward_list&& x);
    template<class Compare> void merge(forward_list& x, Compare comp);
    template<class Compare> void merge(forward_list&& x, Compare comp);

    void sort();
    template<class Compare> void sort(Compare comp);

    void reverse() noexcept;
  };

  template<class InputIterator, class Allocator = allocator<iter-value-type<InputIterator>>>
    forward_list(InputIterator, InputIterator, Allocator = Allocator())
      -> forward_list<iter-value-type<InputIterator>, Allocator>;

  // swap
  template<class T, class Allocator>
    void swap(forward_list<T, Allocator>& x, forward_list<T, Allocator>& y)
      noexcept(noexcept(x.swap(y)));
}
```

**4** An incomplete type `T` may be used when instantiating `forward_list` if the allocator meets the allocator completeness requirements ([[allocator.requirements.completeness]](../library/allocator.requirements.completeness.md)). `T` shall be complete before any member of the resulting specialization of `forward_list` is referenced.
