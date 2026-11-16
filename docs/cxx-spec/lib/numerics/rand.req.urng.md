# 26.6.2.3 Uniform random bit generator requirements [rand.req.urng]

[Contents](../../README.md) · up: [26.6.2 Requirements \[rand.req\]](rand.req.md) · prev: [26.6.2.2 Seed sequence requirements \[rand.req.seedseq\]](rand.req.seedseq.md) · next: [26.6.2.4 Random number engine requirements \[rand.req.eng\]](rand.req.eng.md)

**1** A *uniform random bit generator* `g` of type `G` is a function object returning unsigned integer values such that each value in the range of possible results has (ideally) equal probability of being returned. [*Note 1*: The degree to which `g`’s results approximate the ideal is often determined statistically. — *end note*]

```cpp
template<class G>
  concept uniform_random_bit_generator =
    invocable<G&> && unsigned_integral<invoke_result_t<G&>> &&
    requires {
      { G::min() } -> same_as<invoke_result_t<G&>>;
      { G::max() } -> same_as<invoke_result_t<G&>>;
      requires bool_constant<(G::min() < G::max())>::value;
    };
```

**2** Let `g` be an object of type `G`. `G` models `uniform_random_bit_generator` only if

- `G::min() <= g()`,
- `g() <= G::max()`, and
- `g()` has amortized constant complexity.

**3** A class `G` meets the *uniform random bit generator* requirements if `G` models `uniform_random_bit_generator`, `invoke_result_t<G&>` is an unsigned integer type ([[basic.fundamental]](../../basic/basic.fundamental.md)), and `G` provides a nested *typedef-name* `result_type` that denotes the same type as `invoke_result_t<G&>`.
