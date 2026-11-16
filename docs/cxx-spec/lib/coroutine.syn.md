# 17.12.1 Header `<coroutine>` synopsis [coroutine.syn]

[Contents](../README.md) · up: [17.12 Coroutines \[support.coroutine\]](support.coroutine.md) · prev: [17.12 Coroutines \[support.coroutine\]](support.coroutine.md) · next: [17.12.2 Coroutine traits \[coroutine.traits\]](coroutine.traits.md)

```cpp
#include <compare>              // see [compare.syn]

namespace std {
  // [coroutine.traits], coroutine traits
  template<class R, class... ArgTypes>
    struct coroutine_traits;

  // [coroutine.handle], coroutine handle
  template<class Promise = void>
    struct coroutine_handle;

  // [coroutine.handle.compare], comparison operators
  constexpr bool operator==(coroutine_handle<> x, coroutine_handle<> y) noexcept;
  constexpr strong_ordering operator<=>(coroutine_handle<> x, coroutine_handle<> y) noexcept;

  // [coroutine.handle.hash], hash support
  template<class T> struct hash;
  template<class P> struct hash<coroutine_handle<P>>;

  // [coroutine.noop], no-op coroutines
  struct noop_coroutine_promise;

  template<> struct coroutine_handle<noop_coroutine_promise>;
  using noop_coroutine_handle = coroutine_handle<noop_coroutine_promise>;

  noop_coroutine_handle noop_coroutine() noexcept;

  // [coroutine.trivial.awaitables], trivial awaitables
  struct suspend_never;
  struct suspend_always;
}
```
