# 20.14.17.2 Class template `boyer_moore_searcher` [func.search.bm]

[Contents](../README.md) · up: [20.14.17 Searchers \[func.search\]](func.search.md) · prev: [20.14.17.1 Class template `default_searcher` \[func.search.default\]](func.search.default.md) · next: [20.14.17.3 Class template `boyer_moore_horspool_searcher` \[func.search.bmh\]](func.search.bmh.md)

```cpp
template<class RandomAccessIterator1,
         class Hash = hash<typename iterator_traits<RandomAccessIterator1>::value_type>,
         class BinaryPredicate = equal_to<>>
  class boyer_moore_searcher {
  public:
    boyer_moore_searcher(RandomAccessIterator1 pat_first,
                         RandomAccessIterator1 pat_last,
                         Hash hf = Hash(),
                         BinaryPredicate pred = BinaryPredicate());

    template<class RandomAccessIterator2>
      pair<RandomAccessIterator2, RandomAccessIterator2>
        operator()(RandomAccessIterator2 first, RandomAccessIterator2 last) const;

  private:
    RandomAccessIterator1 pat_first_;   // exposition only
    RandomAccessIterator1 pat_last_;    // exposition only
    Hash hash_;                         // exposition only
    BinaryPredicate pred_;              // exposition only
  };
```

```cpp
boyer_moore_searcher(RandomAccessIterator1 pat_first,
                     RandomAccessIterator1 pat_last,
                     Hash hf = Hash(),
                     BinaryPredicate pred = BinaryPredicate());
```

**1** *Preconditions:* The value type of `RandomAccessIterator1` meets the *Cpp17DefaultConstructible* requirements, the *Cpp17CopyConstructible* requirements, and the *Cpp17CopyAssignable* requirements.

**2** *Preconditions:* Let `V` be `iterator_traits<RandomAccessIterator1>::value_type`. For any two values `A` and `B` of type `V`, if `pred(A, B) == true`, then `hf(A) == hf(B)` is `true`.

**3** *Effects:* Initializes `pat_first_` with `pat_first`, `pat_last_` with `pat_last`, `hash_` with `hf`, and `pred_` with `pred`.

**4** *Throws:* Any exception thrown by the copy constructor of `RandomAccessIterator1`, or by the default constructor, copy constructor, or the copy assignment operator of the value type of `RandomAccessIterator1`, or the copy constructor or `operator()` of `BinaryPredicate` or `Hash`. May throw `bad_alloc` if additional memory needed for internal data structures cannot be allocated.

```cpp
template<class RandomAccessIterator2>
  pair<RandomAccessIterator2, RandomAccessIterator2>
    operator()(RandomAccessIterator2 first, RandomAccessIterator2 last) const;
```

**5** *Mandates:* `RandomAccessIterator1` and `RandomAccessIterator2` have the same value type.

**6** *Effects:* Finds a subsequence of equal values in a sequence.

**7** *Returns:* A pair of iterators `i` and `j` such that

- `i` is the first iterator in the range `[first, last - (pat_last_ - pat_first_))` such that for every non-negative integer `n` less than `pat_last_ - pat_first_` the following condition holds: `pred(*(i + n), *(pat_first_ + n)) != false`, and
- `j == next(i, distance(pat_first_, pat_last_))`.

Returns `make_pair(first, first)` if `[pat_first_, pat_last_)` is empty, otherwise returns `make_pair(last, last)` if no such iterator is found.

**8** *Complexity:* At most `(last - first) * (pat_last_ - pat_first_)` applications of the predicate.
