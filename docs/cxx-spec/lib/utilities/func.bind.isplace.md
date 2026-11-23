# 20.14.14.2 Class template `is_placeholder` [func.bind.isplace]

[Contents](../../README.md) · up: [20.14.14 Function object binders \[func.bind\]](func.bind.md) · prev: [20.14.14.1 Class template `is_bind_expression` \[func.bind.isbind\]](func.bind.isbind.md) · next: [20.14.14.3 Function template `bind` \[func.bind.bind\]](func.bind.bind.md)

```cpp
namespace std {
  template<class T> struct is_placeholder;      // see below
}
```

**1** The class template `is_placeholder` can be used to detect the standard placeholders `_1`, `_2`, and so on. The function template `bind` uses `is_placeholder` to detect placeholders.

**2** Specializations of the `is_placeholder` template shall meet the *Cpp17UnaryTypeTrait* requirements ([[meta.rqmts]](meta.rqmts.md)). The implementation provides a definition that has the base characteristic of `integral_constant<int, J>` if `T` is the type of `std::placeholders::_J`, otherwise it has a base characteristic of `integral_constant<int, 0>`. A program may specialize this template for a program-defined type `T` to have a base characteristic of `integral_constant<int, N>` with `N > 0` to indicate that `T` should be treated as a placeholder type.
