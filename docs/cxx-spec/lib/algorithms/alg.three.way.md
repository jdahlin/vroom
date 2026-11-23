# 25.8.11 Three-way comparison algorithms [alg.three.way]

[Contents](../../README.md) · up: [25.8 Sorting and related operations \[alg.sorting\]](alg.sorting.md) · prev: [25.8.10 Lexicographical comparison \[alg.lex.comparison\]](alg.lex.comparison.md) · next: [25.8.12 Permutation generators \[alg.permutation.generators\]](alg.permutation.generators.md)

```cpp
template<class InputIterator1, class InputIterator2, class Cmp>
  constexpr auto
    lexicographical_compare_three_way(InputIterator1 b1, InputIterator1 e1,
                                      InputIterator2 b2, InputIterator2 e2,
                                      Cmp comp)
      -> decltype(comp(*b1, *b2));
```

**1** *Mandates:* `decltype(comp(*b1, *b2))` is a comparison category type.

**2** *Effects:* Lexicographically compares two ranges and produces a result of the strongest applicable comparison category type. Equivalent to:

```cpp
for ( ; b1 != e1 && b2 != e2; void(++b1), void(++b2) )
  if (auto cmp = comp(*b1,*b2); cmp != 0)
    return cmp;
return b1 != e1 ? strong_ordering::greater :
       b2 != e2 ? strong_ordering::less :
                  strong_ordering::equal;
```

```cpp
template<class InputIterator1, class InputIterator2>
  constexpr auto
    lexicographical_compare_three_way(InputIterator1 b1, InputIterator1 e1,
                                      InputIterator2 b2, InputIterator2 e2);
```

**3** *Effects:* Equivalent to:

```cpp
return lexicographical_compare_three_way(b1, e1, b2, e2, compare_three_way());
```
