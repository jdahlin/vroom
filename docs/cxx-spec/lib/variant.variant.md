# 20.7.3 Class template `variant` [variant.variant]

[Contents](../README.md) · up: [20.7 Variants \[variant\]](variant.md) · prev: [20.7.2 Header `<variant>` synopsis \[variant.syn\]](variant.syn.md) · next: [20.7.3.1 Constructors \[variant.ctor\]](variant.ctor.md)

```cpp
namespace std {
  template<class... Types>
  class variant {
  public:
    // [variant.ctor], constructors
    constexpr variant() noexcept(see below);
    constexpr variant(const variant&);
    constexpr variant(variant&&) noexcept(see below);

    template<class T>
      constexpr variant(T&&) noexcept(see below);

    template<class T, class... Args>
      constexpr explicit variant(in_place_type_t<T>, Args&&...);
    template<class T, class U, class... Args>
      constexpr explicit variant(in_place_type_t<T>, initializer_list<U>, Args&&...);

    template<size_t I, class... Args>
      constexpr explicit variant(in_place_index_t<I>, Args&&...);
    template<size_t I, class U, class... Args>
      constexpr explicit variant(in_place_index_t<I>, initializer_list<U>, Args&&...);

    // [variant.dtor], destructor
    ~variant();

    // [variant.assign], assignment
    constexpr variant& operator=(const variant&);
    constexpr variant& operator=(variant&&) noexcept(see below);

    template<class T> variant& operator=(T&&) noexcept(see below);

    // [variant.mod], modifiers
    template<class T, class... Args>
      T& emplace(Args&&...);
    template<class T, class U, class... Args>
      T& emplace(initializer_list<U>, Args&&...);
    template<size_t I, class... Args>
      variant_alternative_t<I, variant<Types...>>& emplace(Args&&...);
    template<size_t I, class U, class... Args>
      variant_alternative_t<I, variant<Types...>>& emplace(initializer_list<U>, Args&&...);

    // [variant.status], value status
    constexpr bool valueless_by_exception() const noexcept;
    constexpr size_t index() const noexcept;

    // [variant.swap], swap
    void swap(variant&) noexcept(see below);
  };
}
```

**1** Any instance of `variant` at any given time either holds a value of one of its alternative types or holds no value. When an instance of `variant` holds a value of alternative type `T`, it means that a value of type `T`, referred to as the `variant` object’s *contained value*, is allocated within the storage of the `variant` object. Implementations are not permitted to use additional storage, such as dynamic memory, to allocate the contained value. The contained value shall be allocated in a region of the `variant` storage suitably aligned for all types in `Types`.

**2** All types in `Types` shall meet the *Cpp17Destructible* requirements ([Table 32](utility.arg.requirements.md#tab:cpp17.destructible)).

**3** A program that instantiates the definition of `variant` with no template arguments is ill-formed.

## Subclauses

- [20.7.3.1 Constructors \[variant.ctor\]](variant.ctor.md)
- [20.7.3.2 Destructor \[variant.dtor\]](variant.dtor.md)
- [20.7.3.3 Assignment \[variant.assign\]](variant.assign.md)
- [20.7.3.4 Modifiers \[variant.mod\]](variant.mod.md)
- [20.7.3.5 Value status \[variant.status\]](variant.status.md)
- [20.7.3.6 Swap \[variant.swap\]](variant.swap.md)
