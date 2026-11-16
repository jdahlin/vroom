# 22.3.12 Class `vector<bool>` [vector.bool]

[Contents](../README.md) · up: [22.3 Sequence containers \[sequences\]](sequences.md) · prev: [22.3.11.6 Erasure \[vector.erasure\]](vector.erasure.md) · next: [22.4 Associative containers \[associative\]](associative.md)

**1** To optimize space allocation, a specialization of vector for `bool` elements is provided:

```cpp
namespace std {
  template<class Allocator>
  class vector<bool, Allocator> {
  public:
    // types
    using value_type             = bool;
    using allocator_type         = Allocator;
    using pointer                = implementation-defined;
    using const_pointer          = implementation-defined;
    using const_reference        = bool;
    using size_type              = implementation-defined; // see [container.requirements]
    using difference_type        = implementation-defined; // see [container.requirements]
    using iterator               = implementation-defined; // see [container.requirements]
    using const_iterator         = implementation-defined; // see [container.requirements]
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    // bit reference
    class reference {
      friend class vector;
      constexpr reference() noexcept;
    public:
      constexpr reference(const reference&) = default;
      constexpr ~reference();
      constexpr operator bool() const noexcept;
      constexpr reference& operator=(const bool x) noexcept;
      constexpr reference& operator=(const reference& x) noexcept;
      constexpr void flip() noexcept;   // flips the bit
    };

    // construct/copy/destroy
    constexpr vector() : vector(Allocator()) { }
    constexpr explicit vector(const Allocator&);
    constexpr explicit vector(size_type n, const Allocator& = Allocator());
    constexpr vector(size_type n, const bool& value, const Allocator& = Allocator());
    template<class InputIterator>
      constexpr vector(InputIterator first, InputIterator last, const Allocator& = Allocator());
    constexpr vector(const vector& x);
    constexpr vector(vector&& x);
    constexpr vector(const vector&, const Allocator&);
    constexpr vector(vector&&, const Allocator&);
    constexpr vector(initializer_list<bool>, const Allocator& = Allocator()));
    constexpr ~vector();
    constexpr vector& operator=(const vector& x);
    constexpr vector& operator=(vector&& x);
    constexpr vector& operator=(initializer_list<bool>);
    template<class InputIterator>
      constexpr void assign(InputIterator first, InputIterator last);
    constexpr void assign(size_type n, const bool& t);
    constexpr void assign(initializer_list<bool>);
    constexpr allocator_type get_allocator() const noexcept;

    // iterators
    constexpr iterator               begin() noexcept;
    constexpr const_iterator         begin() const noexcept;
    constexpr iterator               end() noexcept;
    constexpr const_iterator         end() const noexcept;
    constexpr reverse_iterator       rbegin() noexcept;
    constexpr const_reverse_iterator rbegin() const noexcept;
    constexpr reverse_iterator       rend() noexcept;
    constexpr const_reverse_iterator rend() const noexcept;

    constexpr const_iterator         cbegin() const noexcept;
    constexpr const_iterator         cend() const noexcept;
    constexpr const_reverse_iterator crbegin() const noexcept;
    constexpr const_reverse_iterator crend() const noexcept;

    // capacity
    [[nodiscard]] constexpr bool empty() const noexcept;
    constexpr size_type size() const noexcept;
    constexpr size_type max_size() const noexcept;
    constexpr size_type capacity() const noexcept;
    constexpr void      resize(size_type sz, bool c = false);
    constexpr void      reserve(size_type n);
    constexpr void      shrink_to_fit();

    // element access
    constexpr reference       operator[](size_type n);
    constexpr const_reference operator[](size_type n) const;
    constexpr const_reference at(size_type n) const;
    constexpr reference       at(size_type n);
    constexpr reference       front();
    constexpr const_reference front() const;
    constexpr reference       back();
    constexpr const_reference back() const;

    // modifiers
    template<class... Args> constexpr reference emplace_back(Args&&... args);
    constexpr void push_back(const bool& x);
    constexpr void pop_back();
    template<class... Args> constexpr iterator emplace(const_iterator position, Args&&... args);
    constexpr iterator insert(const_iterator position, const bool& x);
    constexpr iterator insert(const_iterator position, size_type n, const bool& x);
    template<class InputIterator>
      constexpr iterator insert(const_iterator position,
                                InputIterator first, InputIterator last);
    constexpr iterator insert(const_iterator position, initializer_list<bool> il);

    constexpr iterator erase(const_iterator position);
    constexpr iterator erase(const_iterator first, const_iterator last);
    constexpr void swap(vector&);
    constexpr static void swap(reference x, reference y) noexcept;
    constexpr void flip() noexcept;     // flips all bits
    constexpr void clear() noexcept;
  };
}
```

**2** Unless described below, all operations have the same requirements and semantics as the primary `vector` template, except that operations dealing with the `bool` value type map to bit values in the container storage and `allocator_traits::construct` ([[allocator.traits.members]](allocator.traits.members.md)) is not used to construct these values.

**3** There is no requirement that the data be stored as a contiguous allocation of `bool` values. A space-optimized representation of bits is recommended instead.

**4** `reference` is a class that simulates the behavior of references of a single bit in `vector<bool>`. The conversion function returns `true` when the bit is set, and `false` otherwise. The assignment operator sets the bit when the argument is (convertible to) `true` and clears it otherwise. `flip` reverses the state of the bit.

```cpp
constexpr void flip() noexcept;
```

**5** *Effects:* Replaces each element in the container with its complement.

```cpp
constexpr static void swap(reference x, reference y) noexcept;
```

**6** *Effects:* Exchanges the contents of `x` and `y` as if by:

```cpp
bool b = x;
x = y;
y = b;
```

```cpp
template<class Allocator> struct hash<vector<bool, Allocator>>;
```

**7** The specialization is enabled ([[unord.hash]](unord.hash.md)).
