# 20.14.17.1 Class template `default_searcher` [func.search.default]

[Contents](../../README.md) · up: [20.14.17 Searchers \[func.search\]](func.search.md) · prev: [20.14.17 Searchers \[func.search\]](func.search.md) · next: [20.14.17.2 Class template `boyer_moore_searcher` \[func.search.bm\]](func.search.bm.md)

```cpp
template<class ForwardIterator1, class BinaryPredicate = equal_to<>>
  class default_searcher {
  public:
    constexpr default_searcher(ForwardIterator1 pat_first, ForwardIterator1 pat_last,
                               BinaryPredicate pred = BinaryPredicate());

    template<class ForwardIterator2>
      constexpr pair<ForwardIterator2, ForwardIterator2>
        operator()(ForwardIterator2 first, ForwardIterator2 last) const;

  private:
    ForwardIterator1 pat_first_;        // exposition only
    ForwardIterator1 pat_last_;         // exposition only
    BinaryPredicate pred_;              // exposition only
  };
```

```cpp
constexpr default_searcher(ForwardIterator pat_first, ForwardIterator pat_last,
                           BinaryPredicate pred = BinaryPredicate());
```

**1** *Effects:* Constructs a `default_searcher` object, initializing `pat_first_` with `pat_first`, `pat_last_` with `pat_last`, and `pred_` with `pred`.

**2** *Throws:* Any exception thrown by the copy constructor of `BinaryPredicate` or `ForwardIterator1`.

```cpp
template<class ForwardIterator2>
  constexpr pair<ForwardIterator2, ForwardIterator2>
    operator()(ForwardIterator2 first, ForwardIterator2 last) const;
```

**3** *Effects:* Returns a pair of iterators `i` and `j` such that

- `i == search(first, last, pat_first_, pat_last_, pred_)`, and
- if `i == last`, then `j == last`, otherwise `j == next(i, distance(pat_first_, pat_last_))`.
