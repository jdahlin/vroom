# 23.3.2.3 Iterator traits [iterator.traits]

[Contents](../../README.md) · up: [23.3.2 Associated types \[iterator.assoc.types\]](iterator.assoc.types.md) · prev: [23.3.2.2 Indirectly readable traits \[readable.traits\]](readable.traits.md) · next: [23.3.3 Customization points \[iterator.cust\]](iterator.cust.md)

**1** To implement algorithms only in terms of iterators, it is sometimes necessary to determine the iterator category that corresponds to a particular iterator type. Accordingly, it is required that if `I` is the type of an iterator, the type

```cpp
iterator_traits<I>::iterator_category
```

be defined as the iterator’s iterator category. In addition, the types

```cpp
iterator_traits<I>::pointer
iterator_traits<I>::reference
```

shall be defined as the iterator’s pointer and reference types; that is, for an iterator object `a` of class type, the same type as `decltype(a.operator->())` and `decltype(*a)`, respectively. The type `iterator_traits<I>::pointer` shall be `void` for an iterator of class type `I` that does not support `operator->`. Additionally, in the case of an output iterator, the types

```cpp
iterator_traits<I>::value_type
iterator_traits<I>::difference_type
iterator_traits<I>::reference
```

may be defined as `void`.

**2** The definitions in this subclause make use of the following exposition-only concepts:

```cpp
template<class I>
concept cpp17-iterator =
  copyable<I> && requires(I i) {
    {   *i } -> can-reference;
    {  ++i } -> same_as<I&>;
    { *i++ } -> can-reference;
  };

template<class I>
concept cpp17-input-iterator =
  cpp17-iterator<I> && equality_comparable<I> && requires(I i) {
    typename incrementable_traits<I>::difference_type;
    typename indirectly_readable_traits<I>::value_type;
    typename common_reference_t<iter_reference_t<I>&&,
                                typename indirectly_readable_traits<I>::value_type&>;
    typename common_reference_t<decltype(*i++)&&,
                                typename indirectly_readable_traits<I>::value_type&>;
    requires signed_integral<typename incrementable_traits<I>::difference_type>;
  };

template<class I>
concept cpp17-forward-iterator =
  cpp17-input-iterator<I> && constructible_from<I> &&
  is_lvalue_reference_v<iter_reference_t<I>> &&
  same_as<remove_cvref_t<iter_reference_t<I>>,
          typename indirectly_readable_traits<I>::value_type> &&
  requires(I i) {
    {  i++ } -> convertible_to<const I&>;
    { *i++ } -> same_as<iter_reference_t<I>>;
  };

template<class I>
concept cpp17-bidirectional-iterator =
  cpp17-forward-iterator<I> && requires(I i) {
    {  --i } -> same_as<I&>;
    {  i-- } -> convertible_to<const I&>;
    { *i-- } -> same_as<iter_reference_t<I>>;
  };

template<class I>
concept cpp17-random-access-iterator =
  cpp17-bidirectional-iterator<I> && totally_ordered<I> &&
  requires(I i, typename incrementable_traits<I>::difference_type n) {
    { i += n } -> same_as<I&>;
    { i -= n } -> same_as<I&>;
    { i +  n } -> same_as<I>;
    { n +  i } -> same_as<I>;
    { i -  n } -> same_as<I>;
    { i -  i } -> same_as<decltype(n)>;
    {  i[n]  } -> convertible_to<iter_reference_t<I>>;
  };
```

**3** The members of a specialization `iterator_traits<I>` generated from the `iterator_traits` primary template are computed as follows:

- If `I` has valid ([[temp.deduct]](../../temp/temp.deduct.md)) member types `difference_type`, `value_type`, `reference`, and `iterator_category`, then `iterator_traits<I>` has the following publicly accessible members:

  ```cpp
  using iterator_category = typename I::iterator_category;
  using value_type        = typename I::value_type;
  using difference_type   = typename I::difference_type;
  using pointer           = see below;
  using reference         = typename I::reference;
  ```

  If the *qualified-id* `I::pointer` is valid and denotes a type, then `iterator_traits<I>::pointer` names that type; otherwise, it names `void`.

- Otherwise, if `I` satisfies the exposition-only concept `cpp17-input-iterator`, `iterator_traits<I>` has the following publicly accessible members:

  ```cpp
  using iterator_category = see below;
  using value_type        = typename indirectly_readable_traits<I>::value_type;
  using difference_type   = typename incrementable_traits<I>::difference_type;
  using pointer           = see below;
  using reference         = see below;
  ```

  - If the *qualified-id* `I::pointer` is valid and denotes a type, `pointer` names that type. Otherwise, if `decltype(declval<I&>().operator->())` is well-formed, then `pointer` names that type. Otherwise, `pointer` names `void`.

  - If the *qualified-id* `I::reference` is valid and denotes a type, `reference` names that type. Otherwise, `reference` names `iter_reference_t<I>`.

  - If the *qualified-id* `I::iterator_category` is valid and denotes a type, `iterator_category` names that type. Otherwise, `iterator_category` names:

    - `random_access_iterator_tag` if `I` satisfies `cpp17-random-access-iterator`, or otherwise
    - `bidirectional_iterator_tag` if `I` satisfies `cpp17-bidirectional-iterator`, or otherwise
    - `forward_iterator_tag` if `I` satisfies `cpp17-forward-iterator`, or otherwise
    - `input_iterator_tag`.

- Otherwise, if `I` satisfies the exposition-only concept `cpp17-iterator`, then `iterator_traits<I>` has the following publicly accessible members:

  ```cpp
  using iterator_category = output_iterator_tag;
  using value_type        = void;
  using difference_type   = see below;
  using pointer           = void;
  using reference         = void;
  ```

  If the *qualified-id* `incrementable_traits<I>::difference_type` is valid and denotes a type, then `difference_type` names that type; otherwise, it names `void`.

- Otherwise, `iterator_traits<I>` has no members by any of the above names.

**4** Explicit or partial specializations of `iterator_traits` may have a member type `iterator_concept` that is used to indicate conformance to the iterator concepts ([[iterator.concepts]](iterator.concepts.md)).

**5** `iterator_traits` is specialized for pointers as

```cpp
namespace std {
  template<class T>
    requires is_object_v<T>
  struct iterator_traits<T*> {
    using iterator_concept  = contiguous_iterator_tag;
    using iterator_category = random_access_iterator_tag;
    using value_type        = remove_cv_t<T>;
    using difference_type   = ptrdiff_t;
    using pointer           = T*;
    using reference         = T&;
  };
}
```

**6** [*Example 1*: To implement a generic `reverse` function, a C++ program can do the following:

```cpp
template<class BI>
void reverse(BI first, BI last) {
  typename iterator_traits<BI>::difference_type n =
    distance(first, last);
  --n;
  while(n > 0) {
    typename iterator_traits<BI>::value_type
     tmp = *first;
    *first++ = *--last;
    *last = tmp;
    n -= 2;
  }
}
```

— *end example*]
