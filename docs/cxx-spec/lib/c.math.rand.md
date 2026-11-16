# 26.6.9 Low-quality random number generation [c.math.rand]

[Contents](../README.md) · up: [26.6 Random number generation \[rand\]](rand.md) · prev: [26.6.8.6.3 Class template `piecewise_linear_distribution` \[rand.dist.samp.plinear\]](rand.dist.samp.plinear.md) · next: [26.7 Numeric arrays \[numarray\]](numarray.md)

**1** [*Note 1*: The header `<cstdlib>` ([[cstdlib.syn]](cstdlib.syn.md)) declares the functions described in this subclause. — *end note*]

```cpp
int rand();
void srand(unsigned int seed);
```

**2** *Effects:* The `rand` and `srand` functions have the semantics specified in the C standard library.

**3** *Remarks:* The implementation may specify that particular library functions may call `rand`. It is implementation-defined whether the `rand` function may introduce data races ([[res.on.data.races]](res.on.data.races.md)). [*Note 2*: The other random number generation facilities in this document ([[rand]](rand.md)) are often preferable to `rand`, because `rand`’s underlying algorithm is unspecified. Use of `rand` therefore continues to be non-portable, with unpredictable and oft-questionable quality and performance. — *end note*]

*See also:* ISO C 7.22.2
