# 20.5.3 Class template `tuple` [tuple.tuple]

[Contents](../README.md) · up: [20.5 Tuples \[tuple\]](tuple.md) · prev: [20.5.2 Header `<tuple>` synopsis \[tuple.syn\]](tuple.syn.md) · next: [20.5.3.1 Construction \[tuple.cnstr\]](tuple.cnstr.md)

```cpp
namespace std {
  template<class... Types>
  class tuple {
  public:
    // [tuple.cnstr], tuple construction
    constexpr explicit(see below) tuple();
    constexpr explicit(see below) tuple(const Types&...);         // only if sizeof...(Types) >= 1
    template<class... UTypes>
      constexpr explicit(see below) tuple(UTypes&&...);           // only if sizeof...(Types) >= 1

    tuple(const tuple&) = default;
    tuple(tuple&&) = default;

    template<class... UTypes>
      constexpr explicit(see below) tuple(const tuple<UTypes...>&);
    template<class... UTypes>
      constexpr explicit(see below) tuple(tuple<UTypes...>&&);

    template<class U1, class U2>
      constexpr explicit(see below) tuple(const pair<U1, U2>&);   // only if sizeof...(Types) == 2
    template<class U1, class U2>
      constexpr explicit(see below) tuple(pair<U1, U2>&&);        // only if sizeof...(Types) == 2

    // allocator-extended constructors
    template<class Alloc>
      constexpr explicit(see below)
        tuple(allocator_arg_t, const Alloc& a);
    template<class Alloc>
      constexpr explicit(see below)
        tuple(allocator_arg_t, const Alloc& a, const Types&...);
    template<class Alloc, class... UTypes>
      constexpr explicit(see below)
        tuple(allocator_arg_t, const Alloc& a, UTypes&&...);
    template<class Alloc>
      constexpr tuple(allocator_arg_t, const Alloc& a, const tuple&);
    template<class Alloc>
      constexpr tuple(allocator_arg_t, const Alloc& a, tuple&&);
    template<class Alloc, class... UTypes>
      constexpr explicit(see below)
        tuple(allocator_arg_t, const Alloc& a, const tuple<UTypes...>&);
    template<class Alloc, class... UTypes>
      constexpr explicit(see below)
        tuple(allocator_arg_t, const Alloc& a, tuple<UTypes...>&&);
    template<class Alloc, class U1, class U2>
      constexpr explicit(see below)
        tuple(allocator_arg_t, const Alloc& a, const pair<U1, U2>&);
    template<class Alloc, class U1, class U2>
      constexpr explicit(see below)
        tuple(allocator_arg_t, const Alloc& a, pair<U1, U2>&&);

    // [tuple.assign], tuple assignment
    constexpr tuple& operator=(const tuple&);
    constexpr tuple& operator=(tuple&&) noexcept(see below);

    template<class... UTypes>
      constexpr tuple& operator=(const tuple<UTypes...>&);
    template<class... UTypes>
      constexpr tuple& operator=(tuple<UTypes...>&&);

    template<class U1, class U2>
      constexpr tuple& operator=(const pair<U1, U2>&);          // only if sizeof...(Types) == 2
    template<class U1, class U2>
      constexpr tuple& operator=(pair<U1, U2>&&);               // only if sizeof...(Types) == 2

    // [tuple.swap], tuple swap
    constexpr void swap(tuple&) noexcept(see below);
  };

  template<class... UTypes>
    tuple(UTypes...) -> tuple<UTypes...>;
  template<class T1, class T2>
    tuple(pair<T1, T2>) -> tuple<T1, T2>;
  template<class Alloc, class... UTypes>
    tuple(allocator_arg_t, Alloc, UTypes...) -> tuple<UTypes...>;
  template<class Alloc, class T1, class T2>
    tuple(allocator_arg_t, Alloc, pair<T1, T2>) -> tuple<T1, T2>;
  template<class Alloc, class... UTypes>
    tuple(allocator_arg_t, Alloc, tuple<UTypes...>) -> tuple<UTypes...>;
}
```

## Subclauses

- [20.5.3.1 Construction \[tuple.cnstr\]](tuple.cnstr.md)
- [20.5.3.2 Assignment \[tuple.assign\]](tuple.assign.md)
- [20.5.3.3 `swap` \[tuple.swap\]](tuple.swap.md)
