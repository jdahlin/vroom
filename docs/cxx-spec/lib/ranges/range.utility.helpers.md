# 24.5.1 Helper concepts [range.utility.helpers]

[Contents](../../README.md) · up: [24.5 Range utilities \[range.utility\]](range.utility.md) · prev: [24.5 Range utilities \[range.utility\]](range.utility.md) · next: [24.5.2 View interface \[view.interface\]](view.interface.md)

**1** Many of the types in subclause [[range.utility]](range.utility.md) are specified in terms of the following exposition-only concepts:

```cpp
template<class R>
  concept simple-view =                         // exposition only
    view<R> && range<const R> &&
    same_as<iterator_t<R>, iterator_t<const R>> &&
    same_as<sentinel_t<R>, sentinel_t<const R>>;

template<class I>
  concept has-arrow =                           // exposition only
    input_iterator<I> && (is_pointer_v<I> || requires(I i) { i.operator->(); });

template<class T, class U>
  concept not-same-as =                         // exposition only
    !same_as<remove_cvref_t<T>, remove_cvref_t<U>>;
```
