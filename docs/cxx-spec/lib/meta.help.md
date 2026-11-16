# 20.15.3 Helper classes [meta.help]

[Contents](../README.md) · up: [20.15 Metaprogramming and type traits \[meta\]](meta.md) · prev: [20.15.2 Header `<type_traits>` synopsis \[meta.type.synop\]](meta.type.synop.md) · next: [20.15.4 Unary type traits \[meta.unary\]](meta.unary.md)

```cpp
namespace std {
  template<class T, T v> struct integral_constant {
    static constexpr T value = v;

    using value_type = T;
    using type = integral_constant<T, v>;

    constexpr operator value_type() const noexcept { return value; }
    constexpr value_type operator()() const noexcept { return value; }
  };
}
```

**1** The class template `integral_constant`, alias template `bool_constant`, and its associated *typedef-name*s `true_type` and `false_type` are used as base classes to define the interface for various type traits.
