# 22.3.7.1 Overview [array.overview]

[Contents](../../README.md) · up: [22.3.7 Class template `array` \[array\]](array.md) · prev: [22.3.7 Class template `array` \[array\]](array.md) · next: [22.3.7.2 Constructors, copy, and assignment \[array.cons\]](array.cons.md)

**1** The header `<array>` defines a class template for storing fixed-size sequences of objects. An `array` is a contiguous container ([[container.requirements.general]](container.requirements.general.md)). An instance of `array<T, N>` stores `N` elements of type `T`, so that `size() == N` is an invariant.

**2** An `array` is an aggregate ([[dcl.init.aggr]](../../dcl/dcl.init.aggr.md)) that can be list-initialized with up to `N` elements whose types are convertible to `T`.

**3** An `array` meets all of the requirements of a container and of a reversible container ([[container.requirements]](container.requirements.md)), except that a default constructed `array` object is not empty and that `swap` does not have constant complexity. An `array` meets some of the requirements of a sequence container ([[sequence.reqmts]](sequence.reqmts.md)). Descriptions are provided here only for operations on `array` that are not described in one of these tables and for operations where there is additional semantic information.

**4** `array<T, N>` is a structural type ([[temp.param]](../../temp/temp.param.md)) if `T` is a structural type. Two values `a1` and `a2` of type `array<T, N>` are template-argument-equivalent ([[temp.type]](../../temp/temp.type.md)) if and only if each pair of corresponding elements in `a1` and `a2` are template-argument-equivalent.

**5** The types `iterator` and `const_iterator` meet the constexpr iterator requirements ([[iterator.requirements.general]](../iterators/iterator.requirements.general.md)).

```cpp
namespace std {
  template<class T, size_t N>
  struct array {
    // types
    using value_type             = T;
    using pointer                = T*;
    using const_pointer          = const T*;
    using reference              = T&;
    using const_reference        = const T&;
    using size_type              = size_t;
    using difference_type        = ptrdiff_t;
    using iterator               = implementation-defined; // see [container.requirements]
    using const_iterator         = implementation-defined; // see [container.requirements]
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    // no explicit construct/copy/destroy for aggregate type

    constexpr void fill(const T& u);
    constexpr void swap(array&) noexcept(is_nothrow_swappable_v<T>);

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

    // element access
    constexpr reference       operator[](size_type n);
    constexpr const_reference operator[](size_type n) const;
    constexpr reference       at(size_type n);
    constexpr const_reference at(size_type n) const;
    constexpr reference       front();
    constexpr const_reference front() const;
    constexpr reference       back();
    constexpr const_reference back() const;

    constexpr T *       data() noexcept;
    constexpr const T * data() const noexcept;
  };

  template<class T, class... U>
    array(T, U...) -> array<T, 1 + sizeof...(U)>;
}
```
