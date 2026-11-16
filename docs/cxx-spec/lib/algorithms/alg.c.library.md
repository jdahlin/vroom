# 25.12 C library algorithms [alg.c.library]

[Contents](../../README.md) · up: [25 Algorithms library \[algorithms\]](algorithms.md) · prev: [25.11.8 `destroy` \[specialized.destroy\]](specialized.destroy.md) · next: [26 Numerics library \[numerics\]](../numerics/numerics.md)

**1** [*Note 1*: The header `<cstdlib>` ([[cstdlib.syn]](../support/cstdlib.syn.md)) declares the functions described in this subclause. — *end note*]

```cpp
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              c-compare-pred* compar);
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              compare-pred* compar);
void qsort(void* base, size_t nmemb, size_t size, c-compare-pred* compar);
void qsort(void* base, size_t nmemb, size_t size, compare-pred* compar);
```

**2** *Effects:* These functions have the semantics specified in the C standard library.

**3** *Preconditions:* The objects in the array pointed to by `base` are of trivial type.

**4** *Throws:* Any exception thrown by `compar` ([[res.on.exception.handling]](../library/res.on.exception.handling.md)).

*See also:* ISO C 7.22.5.
