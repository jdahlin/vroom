# 20.8.3 Class `any` [any.class]

[Contents](../../README.md) · up: [20.8 Storage for any type \[any\]](any.md) · prev: [20.8.2 Class `bad_any_cast` \[any.bad.any.cast\]](any.bad.any.cast.md) · next: [20.8.3.1 Construction and destruction \[any.cons\]](any.cons.md)

```cpp
namespace std {
  class any {
  public:
    // [any.cons], construction and destruction
    constexpr any() noexcept;

    any(const any& other);
    any(any&& other) noexcept;

    template<class T>
      any(T&& value);

    template<class T, class... Args>
      explicit any(in_place_type_t<T>, Args&&...);
    template<class T, class U, class... Args>
      explicit any(in_place_type_t<T>, initializer_list<U>, Args&&...);

    ~any();

    // [any.assign], assignments
    any& operator=(const any& rhs);
    any& operator=(any&& rhs) noexcept;

    template<class T>
      any& operator=(T&& rhs);

    // [any.modifiers], modifiers
    template<class T, class... Args>
      decay_t<T>& emplace(Args&&...);
    template<class T, class U, class... Args>
      decay_t<T>& emplace(initializer_list<U>, Args&&...);
    void reset() noexcept;
    void swap(any& rhs) noexcept;

    // [any.observers], observers
    bool has_value() const noexcept;
    const type_info& type() const noexcept;
  };
}
```

**1** An object of class `any` stores an instance of any type that meets the constructor requirements or it has no value, and this is referred to as the *state* of the class `any` object. The stored instance is called the *contained value*. Two states are equivalent if either they both have no value, or they both have a value and the contained values are equivalent.

**2** The non-member `any_cast` functions provide type-safe access to the contained value.

**3** Implementations should avoid the use of dynamically allocated memory for a small contained value. However, any such small-object optimization shall only be applied to types `T` for which `is_nothrow_move_constructible_v<T>` is `true`. [*Example 1*: A contained value of type `int` could be stored in an internal buffer, not in separately-allocated memory. — *end example*]

## Subclauses

- [20.8.3.1 Construction and destruction \[any.cons\]](any.cons.md)
- [20.8.3.2 Assignment \[any.assign\]](any.assign.md)
- [20.8.3.3 Modifiers \[any.modifiers\]](any.modifiers.md)
- [20.8.3.4 Observers \[any.observers\]](any.observers.md)
