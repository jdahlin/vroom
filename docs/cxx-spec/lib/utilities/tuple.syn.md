# 20.5.2 Header `<tuple>` synopsis [tuple.syn]

[Contents](../../README.md) · up: [20.5 Tuples \[tuple\]](tuple.md) · prev: [20.5.1 In general \[tuple.general\]](tuple.general.md) · next: [20.5.3 Class template `tuple` \[tuple.tuple\]](tuple.tuple.md)

```cpp
#include <compare>              // see [compare.syn]

namespace std {
  // [tuple.tuple], class template tuple
  template<class... Types>
    class tuple;

  // [tuple.creation], tuple creation functions
  inline constexpr unspecified ignore;

  template<class... TTypes>
    constexpr tuple<unwrap_ref_decay_t<TTypes>...> make_tuple(TTypes&&...);

  template<class... TTypes>
    constexpr tuple<TTypes&&...> forward_as_tuple(TTypes&&...) noexcept;

  template<class... TTypes>
    constexpr tuple<TTypes&...> tie(TTypes&...) noexcept;

  template<class... Tuples>
    constexpr tuple<CTypes...> tuple_cat(Tuples&&...);

  // [tuple.apply], calling a function with a tuple of arguments
  template<class F, class Tuple>
    constexpr decltype(auto) apply(F&& f, Tuple&& t);

  template<class T, class Tuple>
    constexpr T make_from_tuple(Tuple&& t);

  // [tuple.helper], tuple helper classes
  template<class T> struct tuple_size;                  // not defined
  template<class T> struct tuple_size<const T>;

  template<class... Types> struct tuple_size<tuple<Types...>>;

  template<size_t I, class T> struct tuple_element;     // not defined
  template<size_t I, class T> struct tuple_element<I, const T>;

  template<size_t I, class... Types>
    struct tuple_element<I, tuple<Types...>>;

  template<size_t I, class T>
    using tuple_element_t = typename tuple_element<I, T>::type;

  // [tuple.elem], element access
  template<size_t I, class... Types>
    constexpr tuple_element_t<I, tuple<Types...>>& get(tuple<Types...>&) noexcept;
  template<size_t I, class... Types>
    constexpr tuple_element_t<I, tuple<Types...>>&& get(tuple<Types...>&&) noexcept;
  template<size_t I, class... Types>
    constexpr const tuple_element_t<I, tuple<Types...>>& get(const tuple<Types...>&) noexcept;
  template<size_t I, class... Types>
    constexpr const tuple_element_t<I, tuple<Types...>>&& get(const tuple<Types...>&&) noexcept;
  template<class T, class... Types>
    constexpr T& get(tuple<Types...>& t) noexcept;
  template<class T, class... Types>
    constexpr T&& get(tuple<Types...>&& t) noexcept;
  template<class T, class... Types>
    constexpr const T& get(const tuple<Types...>& t) noexcept;
  template<class T, class... Types>
    constexpr const T&& get(const tuple<Types...>&& t) noexcept;

  // [tuple.rel], relational operators
  template<class... TTypes, class... UTypes>
    constexpr bool operator==(const tuple<TTypes...>&, const tuple<UTypes...>&);
  template<class... TTypes, class... UTypes>
    constexpr common_comparison_category_t<synth-three-way-result<TTypes, UTypes>...>
      operator<=>(const tuple<TTypes...>&, const tuple<UTypes...>&);

  // [tuple.traits], allocator-related traits
  template<class... Types, class Alloc>
    struct uses_allocator<tuple<Types...>, Alloc>;

  // [tuple.special], specialized algorithms
  template<class... Types>
    constexpr void swap(tuple<Types...>& x, tuple<Types...>& y) noexcept(see below);

  // [tuple.helper], tuple helper classes
  template<class T>
    inline constexpr size_t tuple_size_v = tuple_size<T>::value;
}
```
