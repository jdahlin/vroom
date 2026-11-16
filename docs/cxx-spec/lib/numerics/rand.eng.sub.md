# 26.6.3.3 Class template `subtract_with_carry_engine` [rand.eng.sub]

[Contents](../../README.md) · up: [26.6.3 Random number engine class templates \[rand.eng\]](rand.eng.md) · prev: [26.6.3.2 Class template `mersenne_twister_engine` \[rand.eng.mers\]](rand.eng.mers.md) · next: [26.6.4 Random number engine adaptor class templates \[rand.adapt\]](rand.adapt.md)

**1** A `subtract_with_carry_engine` random number engine produces unsigned integer random numbers.

**2** The state `x`<sub>i</sub> of a `subtract_with_carry_engine` object `x` is of size 𝑂(*r*), and consists of a sequence *X* of *r* integer values 0 ≤ *X*<sub>*i*</sub> &lt; *m* = 2<sup>*w*</sup>; all subscripts applied to *X* are to be taken modulo *r*. The state `x`<sub>i</sub> additionally consists of an integer *c* (known as the *carry*) whose value is either 0 or 1.

**3** The state transition is performed as follows:

- Let *Y* = *X*<sub>*i*-*s*</sub> - *X*<sub>*i*-*r*</sub> - *c*.
- Set *X*<sub>*i*</sub> to *y* = *Y* mod *m*. Set *c* to 1 if *Y* &lt; 0, otherwise set *c* to 0.

[*Note 1*: This algorithm corresponds to a modular linear function of the form *TA*(`x`<sub>*i*</sub>) = (*a* · `x`<sub>*i*</sub>) mod *b*, where *b* is of the form *m*<sup>*r*</sup> - *m*<sup>*s*</sup> + 1 and *a* = *b* - (*b* - 1) / *m*. — *end note*]

**4** The generation algorithm is given by *GA*(`x`<sub>*i*</sub>) = *y*, where *y* is the value produced as a result of advancing the engine’s state as described above.

```cpp
template<class UIntType, size_t w, size_t s, size_t r>
  class subtract_with_carry_engine {
  public:
    // types
    using result_type = UIntType;

    // engine characteristics
    static constexpr size_t word_size = w;
    static constexpr size_t short_lag = s;
    static constexpr size_t long_lag = r;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return m - 1; }
    static constexpr result_type default_seed = 19780503u;

    // constructors and seeding functions
    subtract_with_carry_engine() : subtract_with_carry_engine(default_seed) {}
    explicit subtract_with_carry_engine(result_type value);
    template<class Sseq> explicit subtract_with_carry_engine(Sseq& q);
    void seed(result_type value = default_seed);
    template<class Sseq> void seed(Sseq& q);

    // generating functions
    result_type operator()();
    void discard(unsigned long long z);
  };
```

**5** The following relations shall hold: `0u < s`, `s < r`, `0 < w`, and `w <= numeric_limits<UIntType>::digits`.

**6** The textual representation consists of the values of *X*<sub>*i*-*r*</sub>, …, *X*<sub>*i*-1</sub>, in that order, followed by *c*.

```cpp
explicit subtract_with_carry_engine(result_type value);
```

**7** *Effects:* Sets the values of *X*<sub>-*r*</sub>, …, *X*<sub>-1</sub>, in that order, as specified below. If *X*<sub>-1</sub> is then 0, sets *c* to 1; otherwise sets *c* to 0.

To set the values *X*<sub>*k*</sub>, first construct `e`, a `linear_congruential_engine` object, as if by the following definition:

```cpp
linear_congruential_engine<result_type,
                          40014u,0u,2147483563u> e(value == 0u ? default_seed : value);
```

Then, to set each *X*<sub>*k*</sub>, obtain new values *z*<sub>0</sub>, …, *z*<sub>*n*-1</sub> from *n* = ⌈ *w*/32 ⌉ successive invocations of `e` taken modulo 2<sup>32</sup>. Set *X*<sub>*k*</sub> to $\left( \sum_{j=0}^{n-1} z_j \cdot 2^{32j}\right) \bmod m$.

**8** *Complexity:* Exactly *n* · `r` invocations of `e`.

```cpp
template<class Sseq> explicit subtract_with_carry_engine(Sseq& q);
```

**9** *Effects:* With $k = \left\lceil w / 32 \right\rceil$ and *a* an array (or equivalent) of length *r* · *k*, invokes `q.generate(a + 0, a + r · k)` and then, iteratively for *i* = -*r*, …, -1, sets *X*<sub>*i*</sub> to $\left(\sum_{j=0}^{k-1}a_{k(i+r)+j} \cdot 2^{32j} \right) \bmod m$. If *X*<sub>-1</sub> is then 0, sets *c* to 1; otherwise sets *c* to 0.
