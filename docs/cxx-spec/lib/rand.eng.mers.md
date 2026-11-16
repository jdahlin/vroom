# 26.6.3.2 Class template `mersenne_twister_engine` [rand.eng.mers]

[Contents](../README.md) · up: [26.6.3 Random number engine class templates \[rand.eng\]](rand.eng.md) · prev: [26.6.3.1 Class template `linear_congruential_engine` \[rand.eng.lcong\]](rand.eng.lcong.md) · next: [26.6.3.3 Class template `subtract_with_carry_engine` \[rand.eng.sub\]](rand.eng.sub.md)

**1** A `mersenne_twister_engine` random number engine[^1] produces unsigned integer random numbers in the closed interval [0,2<sup>*w*</sup>-1]. The state `x`<sub>i</sub> of a `mersenne_twister_engine` object `x` is of size *n* and consists of a sequence *X* of *n* values of the type delivered by `x`; all subscripts applied to *X* are to be taken modulo *n*.

**2** The transition algorithm employs a twisted generalized feedback shift register defined by shift values *n* and *m*, a twist value *r*, and a conditional xor-mask *a*. To improve the uniformity of the result, the bits of the raw shift register are additionally *tempered* (i.e., scrambled) according to a bit-scrambling matrix defined by values *u*, *d*, *s*, *b*, *t*, *c*, and ℓ.

The state transition is performed as follows:

- Concatenate the upper *w*-*r* bits of *X*<sub>*i*-*n*</sub> with the lower *r* bits of *X*<sub>*i*+1-*n*</sub> to obtain an unsigned integer value *Y*.
- With α = *a* · (*Y* bitand 1), set *X*<sub>*i*</sub> to *X*<sub>*i*+*m*-*n*</sub> xor (*Y* rshift 1) xor α.

The sequence *X* is initialized with the help of an initialization multiplier *f*.

**3** The generation algorithm determines the unsigned integer values *z*<sub>1</sub>, *z*<sub>2</sub>, *z*<sub>3</sub>, *z*<sub>4</sub> as follows, then delivers *z*<sub>4</sub> as its result:

- Let *z*<sub>1</sub> = *X*<sub>*i*</sub> xor (( *X*<sub>*i*</sub> rshift *u* ) bitand *d*).
- Let *z*<sub>2</sub> = *z*<sub>1</sub> xor ( (*z*<sub>1</sub> lshift<sub>*w*</sub> *s*) bitand *b* ).
- Let *z*<sub>3</sub> = *z*<sub>2</sub> xor ( (*z*<sub>2</sub> lshift<sub>*w*</sub> *t*) bitand *c* ).
- Let *z*<sub>4</sub> = *z*<sub>3</sub> xor ( *z*<sub>3</sub> rshift ℓ ).

```cpp
template<class UIntType, size_t w, size_t n, size_t m, size_t r,
         UIntType a, size_t u, UIntType d, size_t s,
         UIntType b, size_t t,
         UIntType c, size_t l, UIntType f>
  class mersenne_twister_engine {
  public:
    // types
    using result_type = UIntType;

    // engine characteristics
    static constexpr size_t word_size = w;
    static constexpr size_t state_size = n;
    static constexpr size_t shift_size = m;
    static constexpr size_t mask_bits = r;
    static constexpr UIntType xor_mask = a;
    static constexpr size_t tempering_u = u;
    static constexpr UIntType tempering_d = d;
    static constexpr size_t tempering_s = s;
    static constexpr UIntType tempering_b = b;
    static constexpr size_t tempering_t = t;
    static constexpr UIntType tempering_c = c;
    static constexpr size_t tempering_l = l;
    static constexpr UIntType initialization_multiplier = f;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return  2^w - 1; }
    static constexpr result_type default_seed = 5489u;

    // constructors and seeding functions
    mersenne_twister_engine() : mersenne_twister_engine(default_seed) {}
    explicit mersenne_twister_engine(result_type value);
    template<class Sseq> explicit mersenne_twister_engine(Sseq& q);
    void seed(result_type value = default_seed);
    template<class Sseq> void seed(Sseq& q);

    // generating functions
    result_type operator()();
    void discard(unsigned long long z);
  };
```

**4** The following relations shall hold: `0 < m`, `m <= n`, `2u < w`, `r <= w`, `u <= w`, `s <= w`, `t <= w`, `l <= w`, `w <= numeric_limits<UIntType>::digits`, `a <= (1u<<w) - 1u`, `b <= (1u<<w) - 1u`, `c <= (1u<<w) - 1u`, `d <= (1u<<w) - 1u`, and `f <= (1u<<w) - 1u`.

**5** The textual representation of `x`<sub>i</sub> consists of the values of *X*<sub>*i* - *n*</sub>, …, *X*<sub>*i* - 1</sub>, in that order.

```cpp
explicit mersenne_twister_engine(result_type value);
```

**6** *Effects:* Sets *X*<sub>-*n*</sub> to `value` mod 2<sup>*w*</sup>. Then, iteratively for *i* = 1 - *n*, …, -1, sets *X*<sub>*i*</sub> to

$$\bigl[f \cdot
       \bigl(X_{i-1} \xor \bigl(X_{i-1} \rightshift (w-2)\bigr)
       \bigr)
       + i \bmod n
 \bigr] \bmod 2^w
\; \mbox{.}$$

**7** *Complexity:* 𝑂(*n*).

```cpp
template<class Sseq> explicit mersenne_twister_engine(Sseq& q);
```

**8** *Effects:* With $k = \left\lceil w / 32 \right\rceil$ and *a* an array (or equivalent) of length *n* · *k*, invokes `q.generate(a+0, a+n · k)` and then, iteratively for *i* = -*n*,…,-1, sets *X*<sub>*i*</sub> to $\left(\sum_{j=0}^{k-1}a_{k(i+n)+j} \cdot 2^{32j} \right) \bmod 2^w$. Finally, if the most significant *w*-*r* bits of *X*<sub>-*n*</sub> are zero, and if each of the other resulting *X*<sub>*i*</sub> is 0, changes *X*<sub>-*n*</sub> to 2<sup>*w*-1</sup>.

[^1]: The name of this engine refers, in part, to a property of its period: For properly-selected values of the parameters, the period is closely related to a large Mersenne prime number.
