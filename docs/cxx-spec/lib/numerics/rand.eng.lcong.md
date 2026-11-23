# 26.6.3.1 Class template `linear_congruential_engine` [rand.eng.lcong]

[Contents](../../README.md) · up: [26.6.3 Random number engine class templates \[rand.eng\]](rand.eng.md) · prev: [26.6.3 Random number engine class templates \[rand.eng\]](rand.eng.md) · next: [26.6.3.2 Class template `mersenne_twister_engine` \[rand.eng.mers\]](rand.eng.mers.md)

**1** A `linear_congruential_engine` random number engine produces unsigned integer random numbers. The state `x`<sub>i</sub> of a `linear_congruential_engine` object `x` is of size 1 and consists of a single integer. The transition algorithm is a modular linear function of the form *TA*(`x`<sub>*i*</sub>) = (*a* · `x`<sub>*i*</sub> + *c*) mod *m*; the generation algorithm is *GA*(`x`<sub>*i*</sub>) = `x`<sub>*i*+1</sub>.

```cpp
template<class UIntType, UIntType a, UIntType c, UIntType m>
  class linear_congruential_engine {
  public:
    // types
    using result_type = UIntType;

    // engine characteristics
    static constexpr result_type multiplier = a;
    static constexpr result_type increment = c;
    static constexpr result_type modulus = m;
    static constexpr result_type min() { return c == 0u ? 1u: 0u; }
    static constexpr result_type max() { return m - 1u; }
    static constexpr result_type default_seed = 1u;

    // constructors and seeding functions
    linear_congruential_engine() : linear_congruential_engine(default_seed) {}
    explicit linear_congruential_engine(result_type s);
    template<class Sseq> explicit linear_congruential_engine(Sseq& q);
    void seed(result_type s = default_seed);
    template<class Sseq> void seed(Sseq& q);

    // generating functions
    result_type operator()();
    void discard(unsigned long long z);
  };
```

**2** If the template parameter `m` is 0, the modulus *m* used throughout this subclause [[rand.eng.lcong]](rand.eng.lcong.md) is `numeric_limits<result_type>::max()` plus 1. [*Note 1*: *m* need not be representable as a value of type `result_type`. — *end note*]

**3** If the template parameter `m` is not 0, the following relations shall hold: `a < m` and `c < m`.

**4** The textual representation consists of the value of `x`<sub>i</sub>.

```cpp
explicit linear_congruential_engine(result_type s);
```

**5** *Effects:* If *c* mod *m* is 0 and `s` mod *m* is 0, sets the engine’s state to 1, otherwise sets the engine’s state to `s` mod *m*.

```cpp
template<class Sseq> explicit linear_congruential_engine(Sseq& q);
```

**6** *Effects:* With $k = \left\lceil \frac{\log_2 m}{32} \right\rceil$ and *a* an array (or equivalent) of length *k* + 3, invokes `q.generate(a + 0, a + k + 3)` and then computes $S = \left(\sum_{j = 0}^{k - 1} a_{j + 3} \cdot 2^{32j} \right) \bmod m$. If *c* mod *m* is 0 and *S* is 0, sets the engine’s state to 1, else sets the engine’s state to *S*.
