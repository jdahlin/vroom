# 24.5.3 Sub-ranges [range.subrange]

[Contents](../../README.md) · up: [24.5 Range utilities \[range.utility\]](range.utility.md) · prev: [24.5.2.1 Members \[view.interface.members\]](view.interface.members.md) · next: [24.5.3.1 Constructors and conversions \[range.subrange.ctor\]](range.subrange.ctor.md)

**1** The `subrange` class template combines together an iterator and a sentinel into a single object that models the `view` concept. Additionally, it models the `sized_range` concept when the final template parameter is `subrange_kind::sized`.

```cpp
namespace std::ranges {
  template<class From, class To>
    concept convertible-to-non-slicing =                    // exposition only
      convertible_to<From, To> &&
      !(is_pointer_v<decay_t<From>> &&
        is_pointer_v<decay_t<To>> &&
        not-same-as<remove_pointer_t<decay_t<From>>, remove_pointer_t<decay_t<To>>>);

  template<class T>
    concept pair-like =                                     // exposition only
      !is_reference_v<T> && requires(T t) {
        typename tuple_size<T>::type;                       // ensures tuple_size<T> is complete
        requires derived_from<tuple_size<T>, integral_constant<size_t, 2>>;
        typename tuple_element_t<0, remove_const_t<T>>;
        typename tuple_element_t<1, remove_const_t<T>>;
        { get<0>(t) } -> convertible_to<const tuple_element_t<0, T>&>;
        { get<1>(t) } -> convertible_to<const tuple_element_t<1, T>&>;
      };

  template<class T, class U, class V>
    concept pair-like-convertible-from =                    // exposition only
      !range<T> && pair-like<T> &&
      constructible_from<T, U, V> &&
      convertible-to-non-slicing<U, tuple_element_t<0, T>> &&
      convertible_to<V, tuple_element_t<1, T>>;

  template<class T>
    concept iterator-sentinel-pair =                        // exposition only
      !range<T> && pair-like<T> &&
      sentinel_for<tuple_element_t<1, T>, tuple_element_t<0, T>>;

  template<input_or_output_iterator I, sentinel_for<I> S = I, subrange_kind K =
      sized_sentinel_for<S, I> ? subrange_kind::sized : subrange_kind::unsized>
    requires (K == subrange_kind::sized || !sized_sentinel_for<S, I>)
  class subrange : public view_interface<subrange<I, S, K>> {
  private:
    static constexpr bool StoreSize =                           // exposition only
      K == subrange_kind::sized && !sized_sentinel_for<S, I>;
    I begin_ = I();                                             // exposition only
    S end_ = S();                                               // exposition only
    make-unsigned-like-t<iter_difference_t<I>> size_ = 0;       // exposition only; present only
                                                                // when StoreSize is true
  public:
    subrange() = default;

    constexpr subrange(convertible-to-non-slicing<I> auto i, S s) requires (!StoreSize);

    constexpr subrange(convertible-to-non-slicing<I> auto i, S s,
                       make-unsigned-like-t<iter_difference_t<I>> n)
      requires (K == subrange_kind::sized);

    template<not-same-as<subrange> R>
      requires borrowed_range<R> &&
               convertible-to-non-slicing<iterator_t<R>, I> &&
               convertible_to<sentinel_t<R>, S>
    constexpr subrange(R&& r) requires (!StoreSize || sized_range<R>);

    template<borrowed_range R>
      requires convertible-to-non-slicing<iterator_t<R>, I> &&
               convertible_to<sentinel_t<R>, S>
    constexpr subrange(R&& r, make-unsigned-like-t<iter_difference_t<I>> n)
      requires (K == subrange_kind::sized)
        : subrange{ranges::begin(r), ranges::end(r), n}
    {}

    template<not-same-as<subrange> PairLike>
      requires pair-like-convertible-from<PairLike, const I&, const S&>
    constexpr operator PairLike() const;

    constexpr I begin() const requires copyable<I>;
    [[nodiscard]] constexpr I begin() requires (!copyable<I>);
    constexpr S end() const;

    constexpr bool empty() const;
    constexpr make-unsigned-like-t<iter_difference_t<I>> size() const
      requires (K == subrange_kind::sized);

    [[nodiscard]] constexpr subrange next(iter_difference_t<I> n = 1) const &
      requires forward_iterator<I>;
    [[nodiscard]] constexpr subrange next(iter_difference_t<I> n = 1) &&;
    [[nodiscard]] constexpr subrange prev(iter_difference_t<I> n = 1) const
      requires bidirectional_iterator<I>;
    constexpr subrange& advance(iter_difference_t<I> n);
  };

  template<input_or_output_iterator I, sentinel_for<I> S>
    subrange(I, S) -> subrange<I, S>;

  template<input_or_output_iterator I, sentinel_for<I> S>
    subrange(I, S, make-unsigned-like-t<iter_difference_t<I>>) ->
      subrange<I, S, subrange_kind::sized>;

  template<iterator-sentinel-pair P>
    subrange(P) -> subrange<tuple_element_t<0, P>, tuple_element_t<1, P>>;

  template<iterator-sentinel-pair P>
    subrange(P, make-unsigned-like-t<iter_difference_t<tuple_element_t<0, P>>>) ->
      subrange<tuple_element_t<0, P>, tuple_element_t<1, P>, subrange_kind::sized>;

  template<borrowed_range R>
    subrange(R&&) ->
      subrange<iterator_t<R>, sentinel_t<R>,
               (sized_range<R> || sized_sentinel_for<sentinel_t<R>, iterator_t<R>>)
                 ? subrange_kind::sized : subrange_kind::unsized>;

  template<borrowed_range R>
    subrange(R&&, make-unsigned-like-t<range_difference_t<R>>) ->
      subrange<iterator_t<R>, sentinel_t<R>, subrange_kind::sized>;

  template<size_t N, class I, class S, subrange_kind K>
    requires (N < 2)
    constexpr auto get(const subrange<I, S, K>& r);

  template<size_t N, class I, class S, subrange_kind K>
    requires (N < 2)
    constexpr auto get(subrange<I, S, K>&& r);
}

namespace std {
  using ranges::get;
}
```

## Subclauses

- [24.5.3.1 Constructors and conversions \[range.subrange.ctor\]](range.subrange.ctor.md)
- [24.5.3.2 Accessors \[range.subrange.access\]](range.subrange.access.md)
