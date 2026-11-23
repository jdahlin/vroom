# 20.6.3 Class template `optional` [optional.optional]

[Contents](../../README.md) · up: [20.6 Optional objects \[optional\]](optional.md) · prev: [20.6.2 Header `<optional>` synopsis \[optional.syn\]](optional.syn.md) · next: [20.6.3.1 Constructors \[optional.ctor\]](optional.ctor.md)

```cpp
namespace std {
  template<class T>
  class optional {
  public:
    using value_type = T;

    // [optional.ctor], constructors
    constexpr optional() noexcept;
    constexpr optional(nullopt_t) noexcept;
    constexpr optional(const optional&);
    constexpr optional(optional&&) noexcept(see below);
    template<class... Args>
      constexpr explicit optional(in_place_t, Args&&...);
    template<class U, class... Args>
      constexpr explicit optional(in_place_t, initializer_list<U>, Args&&...);
    template<class U = T>
      constexpr explicit(see below) optional(U&&);
    template<class U>
      explicit(see below) optional(const optional<U>&);
    template<class U>
      explicit(see below) optional(optional<U>&&);

    // [optional.dtor], destructor
    ~optional();

    // [optional.assign], assignment
    optional& operator=(nullopt_t) noexcept;
    constexpr optional& operator=(const optional&);
    constexpr optional& operator=(optional&&) noexcept(see below);
    template<class U = T> optional& operator=(U&&);
    template<class U> optional& operator=(const optional<U>&);
    template<class U> optional& operator=(optional<U>&&);
    template<class... Args> T& emplace(Args&&...);
    template<class U, class... Args> T& emplace(initializer_list<U>, Args&&...);

    // [optional.swap], swap
    void swap(optional&) noexcept(see below);

    // [optional.observe], observers
    constexpr const T* operator->() const;
    constexpr T* operator->();
    constexpr const T& operator*() const&;
    constexpr T& operator*() &;
    constexpr T&& operator*() &&;
    constexpr const T&& operator*() const&&;
    constexpr explicit operator bool() const noexcept;
    constexpr bool has_value() const noexcept;
    constexpr const T& value() const&;
    constexpr T& value() &;
    constexpr T&& value() &&;
    constexpr const T&& value() const&&;
    template<class U> constexpr T value_or(U&&) const&;
    template<class U> constexpr T value_or(U&&) &&;

    // [optional.mod], modifiers
    void reset() noexcept;

  private:
    T *val;         // exposition only
  };

  template<class T>
    optional(T) -> optional<T>;
}
```

**1** Any instance of `optional<T>` at any given time either contains a value or does not contain a value. When an instance of `optional<T>` *contains a value*, it means that an object of type `T`, referred to as the optional object’s *contained value*, is allocated within the storage of the optional object. Implementations are not permitted to use additional storage, such as dynamic memory, to allocate its contained value. The contained value shall be allocated in a region of the `optional<T>` storage suitably aligned for the type `T`. When an object of type `optional<T>` is contextually converted to `bool`, the conversion returns `true` if the object contains a value; otherwise the conversion returns `false`.

**2** Member `val` is provided for exposition only. When an `optional<T>` object contains a value, `val` points to the contained value.

**3** `T` shall be a type other than *cv* `in_place_t` or *cv* `nullopt_t` that meets the *Cpp17Destructible* requirements ([Table 32](../library/utility.arg.requirements.md#tab:cpp17.destructible)).

## Subclauses

- [20.6.3.1 Constructors \[optional.ctor\]](optional.ctor.md)
- [20.6.3.2 Destructor \[optional.dtor\]](optional.dtor.md)
- [20.6.3.3 Assignment \[optional.assign\]](optional.assign.md)
- [20.6.3.4 Swap \[optional.swap\]](optional.swap.md)
- [20.6.3.5 Observers \[optional.observe\]](optional.observe.md)
- [20.6.3.6 Modifiers \[optional.mod\]](optional.mod.md)
