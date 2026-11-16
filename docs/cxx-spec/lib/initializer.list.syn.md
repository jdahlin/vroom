# 17.10.1 Header `<initializer_list>` synopsis [initializer.list.syn]

[Contents](../README.md) · up: [17.10 Initializer lists \[support.initlist\]](support.initlist.md) · prev: [17.10 Initializer lists \[support.initlist\]](support.initlist.md) · next: [17.10.2 Initializer list constructors \[support.initlist.cons\]](support.initlist.cons.md)

```cpp
namespace std {
  template<class E> class initializer_list {
  public:
    using value_type      = E;
    using reference       = const E&;
    using const_reference = const E&;
    using size_type       = size_t;

    using iterator        = const E*;
    using const_iterator  = const E*;

    constexpr initializer_list() noexcept;

    constexpr size_t size() const noexcept;     // number of elements
    constexpr const E* begin() const noexcept;  // first element
    constexpr const E* end() const noexcept;    // one past the last element
  };

  // [support.initlist.range], initializer list range access
  template<class E> constexpr const E* begin(initializer_list<E> il) noexcept;
  template<class E> constexpr const E* end(initializer_list<E> il) noexcept;
}
```

**1** An object of type `initializer_list<E>` provides access to an array of objects of type `const E`. [*Note 1*: A pair of pointers or a pointer plus a length would be obvious representations for `initializer_list`. `initializer_list` is used to implement initializer lists as specified in [[dcl.init.list]](../dcl.init.list.md). Copying an initializer list does not copy the underlying elements. — *end note*]

**2** If an explicit specialization or partial specialization of `initializer_list` is declared, the program is ill-formed.
