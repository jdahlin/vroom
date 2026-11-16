# 22.3.2 Header `<array>` synopsis [array.syn]

[Contents](../README.md) · up: [22.3 Sequence containers \[sequences\]](sequences.md) · prev: [22.3.1 In general \[sequences.general\]](sequences.general.md) · next: [22.3.3 Header `<deque>` synopsis \[deque.syn\]](deque.syn.md)

```cpp
#include <compare>              // see [compare.syn]
#include <initializer_list>     // see [initializer.list.syn]

namespace std {
  // [array], class template array
  template<class T, size_t N> struct array;

  template<class T, size_t N>
    constexpr bool operator==(const array<T, N>& x, const array<T, N>& y);
  template<class T, size_t N>
    constexpr synth-three-way-result<T>
      operator<=>(const array<T, N>& x, const array<T, N>& y);

  // [array.special], specialized algorithms
  template<class T, size_t N>
    constexpr void swap(array<T, N>& x, array<T, N>& y) noexcept(noexcept(x.swap(y)));

  // [array.creation], array creation functions
  template<class T, size_t N>
    constexpr array<remove_cv_t<T>, N> to_array(T (&a)[N]);
  template<class T, size_t N>
    constexpr array<remove_cv_t<T>, N> to_array(T (&&a)[N]);

  // [array.tuple], tuple interface
  template<class T> struct tuple_size;
  template<size_t I, class T> struct tuple_element;
  template<class T, size_t N>
    struct tuple_size<array<T, N>>;
  template<size_t I, class T, size_t N>
    struct tuple_element<I, array<T, N>>;
  template<size_t I, class T, size_t N>
    constexpr T& get(array<T, N>&) noexcept;
  template<size_t I, class T, size_t N>
    constexpr T&& get(array<T, N>&&) noexcept;
  template<size_t I, class T, size_t N>
    constexpr const T& get(const array<T, N>&) noexcept;
  template<size_t I, class T, size_t N>
    constexpr const T&& get(const array<T, N>&&) noexcept;
}
```
