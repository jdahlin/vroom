# 20.14.5 Class template `reference_wrapper` [refwrap]

[Contents](../../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.4 Function template `invoke` \[func.invoke\]](func.invoke.md) · next: [20.14.5.1 Constructors and destructor \[refwrap.const\]](refwrap.const.md)

```cpp
namespace std {
  template<class T> class reference_wrapper {
  public:
    // types
    using type = T;

    // construct/copy/destroy
    template<class U>
      constexpr reference_wrapper(U&&) noexcept(see below);
    constexpr reference_wrapper(const reference_wrapper& x) noexcept;

    // assignment
    constexpr reference_wrapper& operator=(const reference_wrapper& x) noexcept;

    // access
    constexpr operator T& () const noexcept;
    constexpr T& get() const noexcept;

    // invocation
    template<class... ArgTypes>
      constexpr invoke_result_t<T&, ArgTypes...> operator()(ArgTypes&&...) const;
  };
  template<class T>
    reference_wrapper(T&) -> reference_wrapper<T>;
}
```

**1** `reference_wrapper<T>` is a *Cpp17CopyConstructible* and *Cpp17CopyAssignable* wrapper around a reference to an object or function of type `T`.

**2** `reference_wrapper<T>` is a trivially copyable type ([[basic.types]](../../basic/basic.types.md)).

**3** The template parameter `T` of `reference_wrapper` may be an incomplete type.

## Subclauses

- [20.14.5.1 Constructors and destructor \[refwrap.const\]](refwrap.const.md)
- [20.14.5.2 Assignment \[refwrap.assign\]](refwrap.assign.md)
- [20.14.5.3 Access \[refwrap.access\]](refwrap.access.md)
- [20.14.5.4 Invocation \[refwrap.invoke\]](refwrap.invoke.md)
- [20.14.5.5 Helper functions \[refwrap.helpers\]](refwrap.helpers.md)
