# 16.4.2.1 Exposition-only functions [expos.only.func]

[Contents](../../README.md) · up: [16.4.2 Other conventions \[conventions\]](conventions.md) · prev: [16.4.2 Other conventions \[conventions\]](conventions.md) · next: [16.4.2.2 Type descriptions \[type.descriptions\]](type.descriptions.md)

**1** Several function templates defined in [[support]](../support/support.md) through [[thread]](../thread/thread.md) and [[depr]](../../depr/depr.md) are only defined for the purpose of exposition. The declaration of such a function is followed by a comment ending in *exposition only*.

**2** The following are defined for exposition only to aid in the specification of the library:

```cpp
template<class T> constexpr decay_t<T> decay-copy(T&& v)
    noexcept(is_nothrow_convertible_v<T, decay_t<T>>)           // exposition only
  { return std::forward<T>(v); }

constexpr auto synth-three-way =
  []<class T, class U>(const T& t, const U& u)
    requires requires {
      { t < u } -> boolean-testable;
      { u < t } -> boolean-testable;
    }
  {
    if constexpr (three_way_comparable_with<T, U>) {
      return t <=> u;
    } else {
      if (t < u) return weak_ordering::less;
      if (u < t) return weak_ordering::greater;
      return weak_ordering::equivalent;
    }
  };

template<class T, class U=T>
using synth-three-way-result = decltype(synth-three-way(declval<T&>(), declval<U&>()));
```
