# 20.14.17 Searchers [func.search]

[Contents](../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.16.2.7 Specialized algorithms \[func.wrap.func.alg\]](func.wrap.func.alg.md) · next: [20.14.17.1 Class template `default_searcher` \[func.search.default\]](func.search.default.md)

**1** This subclause provides function object types ([[function.objects]](function.objects.md)) for operations that search for a sequence `[pat_first, pat_last)` in another sequence `[first, last)` that is provided to the object’s function call operator. The first sequence (the pattern to be searched for) is provided to the object’s constructor, and the second (the sequence to be searched) is provided to the function call operator.

**2** Each specialization of a class template specified in this subclause [[func.search]](func.search.md) shall meet the *Cpp17CopyConstructible* and *Cpp17CopyAssignable* requirements. Template parameters named

- `ForwardIterator`,
- `ForwardIterator1`,
- `ForwardIterator2`,
- `RandomAccessIterator`,
- `RandomAccessIterator1`,
- `RandomAccessIterator2`, and
- `BinaryPredicate`

of templates specified in this subclause [[func.search]](func.search.md) shall meet the same requirements and semantics as specified in [[algorithms.general]](algorithms.general.md). Template parameters named `Hash` shall meet the *Cpp17Hash* requirements ([Table 34](hash.requirements.md#tab:cpp17.hash)).

**3** The Boyer-Moore searcher implements the Boyer-Moore search algorithm. The Boyer-Moore-Horspool searcher implements the Boyer-Moore-Horspool search algorithm. In general, the Boyer-Moore searcher will use more memory and give better runtime performance than Boyer-Moore-Horspool.

## Subclauses

- [20.14.17.1 Class template `default_searcher` \[func.search.default\]](func.search.default.md)
- [20.14.17.2 Class template `boyer_moore_searcher` \[func.search.bm\]](func.search.bm.md)
- [20.14.17.3 Class template `boyer_moore_horspool_searcher` \[func.search.bmh\]](func.search.bmh.md)
