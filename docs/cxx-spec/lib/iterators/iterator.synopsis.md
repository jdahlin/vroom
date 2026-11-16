# 23.2 Header `<iterator>` synopsis [iterator.synopsis]

[Contents](../../README.md) · up: [23 Iterators library \[iterators\]](iterators.md) · prev: [23.1 General \[iterators.general\]](iterators.general.md) · next: [23.3 Iterator requirements \[iterator.requirements\]](iterator.requirements.md)

```cpp
#include <compare>              // see [compare.syn]
#include <concepts>             // see [concepts.syn]

namespace std {
  template<class T> using with-reference = T&;  // exposition only
  template<class T> concept can-reference       // exposition only
    = requires { typename with-reference<T>; };
  template<class T> concept dereferenceable     // exposition only
    = requires(T& t) {
      { *t } -> can-reference;  // not required to be equality-preserving
    };

  // [iterator.assoc.types], associated types
  // [incrementable.traits], incrementable traits
  template<class> struct incrementable_traits;
  template<class T>
    using iter_difference_t = see below;

  // [readable.traits], indirectly readable traits
  template<class> struct indirectly_readable_traits;
  template<class T>
    using iter_value_t = see below;

  // [iterator.traits], iterator traits
  template<class I> struct iterator_traits;
  template<class T> requires is_object_v<T> struct iterator_traits<T*>;

  template<dereferenceable T>
    using iter_reference_t = decltype(*declval<T&>());

  namespace ranges {
    // [iterator.cust], customization points
    inline namespace unspecified {
      // [iterator.cust.move], ranges::iter_move
      inline constexpr unspecified iter_move = unspecified;

      // [iterator.cust.swap], ranges::iter_swap
      inline constexpr unspecified iter_swap = unspecified;
    }
  }

  template<dereferenceable T>
    requires requires(T& t) {
      { ranges::iter_move(t) } -> can-reference;
    }
  using iter_rvalue_reference_t
    = decltype(ranges::iter_move(declval<T&>()));

  // [iterator.concepts], iterator concepts
  // [iterator.concept.readable], concept indirectly_readable
  template<class In>
    concept indirectly_readable = see below;

  template<indirectly_readable T>
    using iter_common_reference_t =
      common_reference_t<iter_reference_t<T>, iter_value_t<T>&>;

  // [iterator.concept.writable], concept indirectly_writable
  template<class Out, class T>
    concept indirectly_writable = see below;

  // [iterator.concept.winc], concept weakly_incrementable
  template<class I>
    concept weakly_incrementable = see below;

  // [iterator.concept.inc], concept incrementable
  template<class I>
    concept incrementable = see below;

  // [iterator.concept.iterator], concept input_or_output_iterator
  template<class I>
    concept input_or_output_iterator = see below;

  // [iterator.concept.sentinel], concept sentinel_for
  template<class S, class I>
    concept sentinel_for = see below;

  // [iterator.concept.sizedsentinel], concept sized_sentinel_for
  template<class S, class I>
    inline constexpr bool disable_sized_sentinel_for = false;

  template<class S, class I>
    concept sized_sentinel_for = see below;

  // [iterator.concept.input], concept input_iterator
  template<class I>
    concept input_iterator = see below;

  // [iterator.concept.output], concept output_iterator
  template<class I, class T>
    concept output_iterator = see below;

  // [iterator.concept.forward], concept forward_iterator
  template<class I>
    concept forward_iterator = see below;

  // [iterator.concept.bidir], concept bidirectional_iterator
  template<class I>
    concept bidirectional_iterator = see below;

  // [iterator.concept.random.access], concept random_access_iterator
  template<class I>
    concept random_access_iterator = see below;

  // [iterator.concept.contiguous], concept contiguous_iterator
  template<class I>
    concept contiguous_iterator = see below;

  // [indirectcallable], indirect callable requirements
  // [indirectcallable.indirectinvocable], indirect callables
  template<class F, class I>
    concept indirectly_unary_invocable = see below;

  template<class F, class I>
    concept indirectly_regular_unary_invocable = see below;

  template<class F, class I>
    concept indirect_unary_predicate = see below;

  template<class F, class I1, class I2>
    concept indirect_binary_predicate = see below;

  template<class F, class I1, class I2 = I1>
    concept indirect_equivalence_relation = see below;

  template<class F, class I1, class I2 = I1>
    concept indirect_strict_weak_order = see below;

  template<class F, class... Is>
    requires (indirectly_readable<Is> && ...) && invocable<F, iter_reference_t<Is>...>
      using indirect_result_t = invoke_result_t<F, iter_reference_t<Is>...>;

  // [projected], projected
  template<indirectly_readable I, indirectly_regular_unary_invocable<I> Proj>
    struct projected;

  template<weakly_incrementable I, class Proj>
    struct incrementable_traits<projected<I, Proj>>;

  // [alg.req], common algorithm requirements
  // [alg.req.ind.move], concept indirectly_movable
  template<class In, class Out>
    concept indirectly_movable = see below;

  template<class In, class Out>
    concept indirectly_movable_storable = see below;

  // [alg.req.ind.copy], concept indirectly_copyable
  template<class In, class Out>
    concept indirectly_copyable = see below;

  template<class In, class Out>
    concept indirectly_copyable_storable = see below;

  // [alg.req.ind.swap], concept indirectly_swappable
  template<class I1, class I2 = I1>
    concept indirectly_swappable = see below;

  // [alg.req.ind.cmp], concept indirectly_comparable
  template<class I1, class I2, class R, class P1 = identity, class P2 = identity>
    concept indirectly_comparable = see below;

  // [alg.req.permutable], concept permutable
  template<class I>
    concept permutable = see below;

  // [alg.req.mergeable], concept mergeable
  template<class I1, class I2, class Out,
      class R = ranges::less, class P1 = identity, class P2 = identity>
    concept mergeable = see below;

  // [alg.req.sortable], concept sortable
  template<class I, class R = ranges::less, class P = identity>
    concept sortable = see below;

  // [iterator.primitives], primitives
  // [std.iterator.tags], iterator tags
  struct input_iterator_tag { };
  struct output_iterator_tag { };
  struct forward_iterator_tag: public input_iterator_tag { };
  struct bidirectional_iterator_tag: public forward_iterator_tag { };
  struct random_access_iterator_tag: public bidirectional_iterator_tag { };
  struct contiguous_iterator_tag: public random_access_iterator_tag { };

  // [iterator.operations], iterator operations
  template<class InputIterator, class Distance>
    constexpr void
      advance(InputIterator& i, Distance n);
  template<class InputIterator>
    constexpr typename iterator_traits<InputIterator>::difference_type
      distance(InputIterator first, InputIterator last);
  template<class InputIterator>
    constexpr InputIterator
      next(InputIterator x,
           typename iterator_traits<InputIterator>::difference_type n = 1);
  template<class BidirectionalIterator>
    constexpr BidirectionalIterator
      prev(BidirectionalIterator x,
           typename iterator_traits<BidirectionalIterator>::difference_type n = 1);

  // [range.iter.ops], range iterator operations
  namespace ranges {
    // [range.iter.op.advance], ranges::advance
    template<input_or_output_iterator I>
      constexpr void advance(I& i, iter_difference_t<I> n);
    template<input_or_output_iterator I, sentinel_for<I> S>
      constexpr void advance(I& i, S bound);
    template<input_or_output_iterator I, sentinel_for<I> S>
      constexpr iter_difference_t<I> advance(I& i, iter_difference_t<I> n, S bound);

    // [range.iter.op.distance], ranges::distance
    template<input_or_output_iterator I, sentinel_for<I> S>
      constexpr iter_difference_t<I> distance(I first, S last);
    template<range R>
      constexpr range_difference_t<R> distance(R&& r);

    // [range.iter.op.next], ranges::next
    template<input_or_output_iterator I>
      constexpr I next(I x);
    template<input_or_output_iterator I>
      constexpr I next(I x, iter_difference_t<I> n);
    template<input_or_output_iterator I, sentinel_for<I> S>
      constexpr I next(I x, S bound);
    template<input_or_output_iterator I, sentinel_for<I> S>
      constexpr I next(I x, iter_difference_t<I> n, S bound);

    // [range.iter.op.prev], ranges::prev
    template<bidirectional_iterator I>
      constexpr I prev(I x);
    template<bidirectional_iterator I>
      constexpr I prev(I x, iter_difference_t<I> n);
    template<bidirectional_iterator I>
      constexpr I prev(I x, iter_difference_t<I> n, I bound);
  }

  // [predef.iterators], predefined iterators and sentinels
  // [reverse.iterators], reverse iterators
  template<class Iterator> class reverse_iterator;

  template<class Iterator1, class Iterator2>
    constexpr bool operator==(
      const reverse_iterator<Iterator1>& x,
      const reverse_iterator<Iterator2>& y);
  template<class Iterator1, class Iterator2>
    constexpr bool operator!=(
      const reverse_iterator<Iterator1>& x,
      const reverse_iterator<Iterator2>& y);
  template<class Iterator1, class Iterator2>
    constexpr bool operator<(
      const reverse_iterator<Iterator1>& x,
      const reverse_iterator<Iterator2>& y);
  template<class Iterator1, class Iterator2>
    constexpr bool operator>(
      const reverse_iterator<Iterator1>& x,
      const reverse_iterator<Iterator2>& y);
  template<class Iterator1, class Iterator2>
    constexpr bool operator<=(
      const reverse_iterator<Iterator1>& x,
      const reverse_iterator<Iterator2>& y);
  template<class Iterator1, class Iterator2>
    constexpr bool operator>=(
      const reverse_iterator<Iterator1>& x,
      const reverse_iterator<Iterator2>& y);
  template<class Iterator1, three_way_comparable_with<Iterator1> Iterator2>
    constexpr compare_three_way_result_t<Iterator1, Iterator2>
      operator<=>(const reverse_iterator<Iterator1>& x,
                  const reverse_iterator<Iterator2>& y);

  template<class Iterator1, class Iterator2>
    constexpr auto operator-(
      const reverse_iterator<Iterator1>& x,
      const reverse_iterator<Iterator2>& y) -> decltype(y.base() - x.base());
  template<class Iterator>
    constexpr reverse_iterator<Iterator>
      operator+(
    typename reverse_iterator<Iterator>::difference_type n,
    const reverse_iterator<Iterator>& x);

  template<class Iterator>
    constexpr reverse_iterator<Iterator> make_reverse_iterator(Iterator i);

  template<class Iterator1, class Iterator2>
      requires (!sized_sentinel_for<Iterator1, Iterator2>)
    inline constexpr bool disable_sized_sentinel_for<reverse_iterator<Iterator1>,
                                                     reverse_iterator<Iterator2>> = true;

  // [insert.iterators], insert iterators
  template<class Container> class back_insert_iterator;
  template<class Container>
    constexpr back_insert_iterator<Container> back_inserter(Container& x);

  template<class Container> class front_insert_iterator;
  template<class Container>
    constexpr front_insert_iterator<Container> front_inserter(Container& x);

  template<class Container> class insert_iterator;
  template<class Container>
    constexpr insert_iterator<Container>
      inserter(Container& x, ranges::iterator_t<Container> i);

  // [move.iterators], move iterators and sentinels
  template<class Iterator> class move_iterator;

  template<class Iterator1, class Iterator2>
    constexpr bool operator==(
      const move_iterator<Iterator1>& x, const move_iterator<Iterator2>& y);
  template<class Iterator1, class Iterator2>
    constexpr bool operator<(
      const move_iterator<Iterator1>& x, const move_iterator<Iterator2>& y);
  template<class Iterator1, class Iterator2>
    constexpr bool operator>(
      const move_iterator<Iterator1>& x, const move_iterator<Iterator2>& y);
  template<class Iterator1, class Iterator2>
    constexpr bool operator<=(
      const move_iterator<Iterator1>& x, const move_iterator<Iterator2>& y);
  template<class Iterator1, class Iterator2>
    constexpr bool operator>=(
      const move_iterator<Iterator1>& x, const move_iterator<Iterator2>& y);
  template<class Iterator1, three_way_comparable_with<Iterator1> Iterator2>
    constexpr compare_three_way_result_t<Iterator1, Iterator2>
      operator<=>(const move_iterator<Iterator1>& x,
                  const move_iterator<Iterator2>& y);

  template<class Iterator1, class Iterator2>
    constexpr auto operator-(
    const move_iterator<Iterator1>& x,
    const move_iterator<Iterator2>& y) -> decltype(x.base() - y.base());
  template<class Iterator>
    constexpr move_iterator<Iterator> operator+(
      typename move_iterator<Iterator>::difference_type n, const move_iterator<Iterator>& x);

  template<class Iterator>
    constexpr move_iterator<Iterator> make_move_iterator(Iterator i);

  template<semiregular S> class move_sentinel;

  // [iterators.common], common iterators
  template<input_or_output_iterator I, sentinel_for<I> S>
    requires (!same_as<I, S> && copyable<I>)
      class common_iterator;

  template<class I, class S>
    struct incrementable_traits<common_iterator<I, S>>;

  template<input_iterator I, class S>
    struct iterator_traits<common_iterator<I, S>>;

  // [default.sentinels], default sentinels
  struct default_sentinel_t;
  inline constexpr default_sentinel_t default_sentinel{};

  // [iterators.counted], counted iterators
  template<input_or_output_iterator I> class counted_iterator;

  template<class I>
    struct incrementable_traits<counted_iterator<I>>;

  template<input_iterator I>
    struct iterator_traits<counted_iterator<I>>;

  // [unreachable.sentinels], unreachable sentinels
  struct unreachable_sentinel_t;
  inline constexpr unreachable_sentinel_t unreachable_sentinel{};

  // [stream.iterators], stream iterators
  template<class T, class charT = char, class traits = char_traits<charT>,
           class Distance = ptrdiff_t>
  class istream_iterator;
  template<class T, class charT, class traits, class Distance>
    bool operator==(const istream_iterator<T,charT,traits,Distance>& x,
            const istream_iterator<T,charT,traits,Distance>& y);

  template<class T, class charT = char, class traits = char_traits<charT>>
      class ostream_iterator;

  template<class charT, class traits = char_traits<charT>>
    class istreambuf_iterator;
  template<class charT, class traits>
    bool operator==(const istreambuf_iterator<charT,traits>& a,
            const istreambuf_iterator<charT,traits>& b);

  template<class charT, class traits = char_traits<charT>>
    class ostreambuf_iterator;

  // [iterator.range], range access
  template<class C> constexpr auto begin(C& c) -> decltype(c.begin());
  template<class C> constexpr auto begin(const C& c) -> decltype(c.begin());
  template<class C> constexpr auto end(C& c) -> decltype(c.end());
  template<class C> constexpr auto end(const C& c) -> decltype(c.end());
  template<class T, size_t N> constexpr T* begin(T (&array)[N]) noexcept;
  template<class T, size_t N> constexpr T* end(T (&array)[N]) noexcept;
  template<class C> constexpr auto cbegin(const C& c) noexcept(noexcept(std::begin(c)))
    -> decltype(std::begin(c));
  template<class C> constexpr auto cend(const C& c) noexcept(noexcept(std::end(c)))
    -> decltype(std::end(c));
  template<class C> constexpr auto rbegin(C& c) -> decltype(c.rbegin());
  template<class C> constexpr auto rbegin(const C& c) -> decltype(c.rbegin());
  template<class C> constexpr auto rend(C& c) -> decltype(c.rend());
  template<class C> constexpr auto rend(const C& c) -> decltype(c.rend());
  template<class T, size_t N> constexpr reverse_iterator<T*> rbegin(T (&array)[N]);
  template<class T, size_t N> constexpr reverse_iterator<T*> rend(T (&array)[N]);
  template<class E> constexpr reverse_iterator<const E*> rbegin(initializer_list<E> il);
  template<class E> constexpr reverse_iterator<const E*> rend(initializer_list<E> il);
  template<class C> constexpr auto crbegin(const C& c) -> decltype(std::rbegin(c));
  template<class C> constexpr auto crend(const C& c) -> decltype(std::rend(c));

  template<class C> constexpr auto size(const C& c) -> decltype(c.size());
  template<class T, size_t N> constexpr size_t size(const T (&array)[N]) noexcept;
  template<class C> constexpr auto ssize(const C& c)
    -> common_type_t<ptrdiff_t, make_signed_t<decltype(c.size())>>;
  template<class T, ptrdiff_t N> constexpr ptrdiff_t ssize(const T (&array)[N]) noexcept;
  template<class C> [[nodiscard]] constexpr auto empty(const C& c) -> decltype(c.empty());
  template<class T, size_t N> [[nodiscard]] constexpr bool empty(const T (&array)[N]) noexcept;
  template<class E> [[nodiscard]] constexpr bool empty(initializer_list<E> il) noexcept;
  template<class C> constexpr auto data(C& c) -> decltype(c.data());
  template<class C> constexpr auto data(const C& c) -> decltype(c.data());
  template<class T, size_t N> constexpr T* data(T (&array)[N]) noexcept;
  template<class E> constexpr const E* data(initializer_list<E> il) noexcept;
}
```
