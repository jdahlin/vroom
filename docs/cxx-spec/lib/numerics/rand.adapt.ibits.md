# 26.6.4.3 Class template `independent_bits_engine` [rand.adapt.ibits]

[Contents](../../README.md) · up: [26.6.4 Random number engine adaptor class templates \[rand.adapt\]](rand.adapt.md) · prev: [26.6.4.2 Class template `discard_block_engine` \[rand.adapt.disc\]](rand.adapt.disc.md) · next: [26.6.4.4 Class template `shuffle_order_engine` \[rand.adapt.shuf\]](rand.adapt.shuf.md)

**1** An `independent_bits_engine` random number engine adaptor combines random numbers that are produced by some base engine *e*, so as to produce random numbers with a specified number of bits *w*. The state `x`<sub>i</sub> of an `independent_bits_engine` engine adaptor object `x` consists of the state `e`<sub>i</sub> of its base engine `e`; the size of the state is the size of *e*’s state.

**2** The transition and generation algorithms are described in terms of the following integral constants:

- Let *R* = `e.max() - e.min() + 1` and $m = \left\lfloor \log_2 R \right\rfloor$.
- With *n* as determined below, let $w_0 = \left\lfloor w / n \right\rfloor$, *n*<sub>0</sub> = *n* - *w* mod *n*, $y_0 = 2^{w_0} \left\lfloor R / 2^{w_0} \right\rfloor$, and $y_1 = 2^{w_0 + 1} \left\lfloor R / 2^{w_0 + 1} \right\rfloor$.
- Let $n = \left\lceil w / m \right\rceil$ if and only if the relation $R - y_0 \leq \left\lfloor y_0 / n \right\rfloor$ holds as a result. Otherwise let $n = 1 + \left\lceil w / m \right\rceil$.

[*Note 1*: The relation *w* = *n*<sub>0</sub> *w*<sub>0</sub> + (*n* - *n*<sub>0</sub>)(*w*<sub>0</sub> + 1) always holds. — *end note*]

**3** The transition algorithm is carried out by invoking `e()` as often as needed to obtain *n*<sub>0</sub> values less than *y*<sub>0</sub> + `e.min()` and *n* - *n*<sub>0</sub> values less than *y*<sub>1</sub> + `e.min()`.

**4** The generation algorithm uses the values produced while advancing the state as described above to yield a quantity *S* obtained as if by the following algorithm:

```cpp
S = 0;
for (k = 0; k ≠ n_0; k += 1)  {
 do u = e() - e.min(); while (u ≥ y_0);
 S = 2^(w_0) · S + u mod 2^(w_0);
}
for (k = n_0; k ≠ n; k += 1)  {
 do u = e() - e.min(); while (u ≥ y_1);
 S = 2^(w_0 + 1) · S + u mod 2^(w_0 + 1);
}
```

```cpp
template<class Engine, size_t w, class UIntType>
  class independent_bits_engine {
  public:
    // types
    using result_type = UIntType;

    // engine characteristics
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return 2^w - 1; }

    // constructors and seeding functions
    independent_bits_engine();
    explicit independent_bits_engine(const Engine& e);
    explicit independent_bits_engine(Engine&& e);
    explicit independent_bits_engine(result_type s);
    template<class Sseq> explicit independent_bits_engine(Sseq& q);
    void seed();
    void seed(result_type s);
    template<class Sseq> void seed(Sseq& q);

    // generating functions
    result_type operator()();
    void discard(unsigned long long z);

    // property functions
    const Engine& base() const noexcept { return e; };

  private:
    Engine e;   // exposition only
  };
```

**5** The following relations shall hold: `0 < w` and `w <= numeric_limits<result_type>::digits`.

**6** The textual representation consists of the textual representation of `e`.
