# 23.5.3.9 Class template `move_sentinel` [move.sentinel]

[Contents](../../README.md) · up: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · prev: [23.5.3.8 Non-member functions \[move.iter.nonmember\]](move.iter.nonmember.md) · next: [23.5.3.10 Operations \[move.sent.ops\]](move.sent.ops.md)

**1** Class template `move_sentinel` is a sentinel adaptor useful for denoting ranges together with `move_iterator`. When an input iterator type `I` and sentinel type `S` model `sentinel_for<S, I>`, `move_sentinel<S>` and `move_iterator<I>` model `sentinel_for<move_sentinel<S>, move_iterator<I>>` as well.

**2** [*Example 1*: A `move_if` algorithm is easily implemented with `copy_if` using `move_iterator` and `move_sentinel`:

```cpp
template<input_iterator I, sentinel_for<I> S, weakly_incrementable O,
         indirect_unary_predicate<I> Pred>
  requires indirectly_movable<I, O>
void move_if(I first, S last, O out, Pred pred) {
  std::ranges::copy_if(move_iterator<I>{first}, move_sentinel<S>{last}, out, pred);
}
```

— *end example*]

```cpp
namespace std {
  template<semiregular S>
  class move_sentinel {
  public:
    constexpr move_sentinel();
    constexpr explicit move_sentinel(S s);
    template<class S2>
      requires convertible_to<const S2&, S>
        constexpr move_sentinel(const move_sentinel<S2>& s);
    template<class S2>
      requires assignable_from<S&, const S2&>
        constexpr move_sentinel& operator=(const move_sentinel<S2>& s);

    constexpr S base() const;
  private:
    S last;     // exposition only
  };
}
```
