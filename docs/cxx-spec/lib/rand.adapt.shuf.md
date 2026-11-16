# 26.6.4.4 Class template `shuffle_order_engine` [rand.adapt.shuf]

[Contents](../README.md) · up: [26.6.4 Random number engine adaptor class templates \[rand.adapt\]](rand.adapt.md) · prev: [26.6.4.3 Class template `independent_bits_engine` \[rand.adapt.ibits\]](rand.adapt.ibits.md) · next: [26.6.5 Engines and engine adaptors with predefined parameters \[rand.predef\]](rand.predef.md)

**1** A `shuffle_order_engine` random number engine adaptor produces the same random numbers that are produced by some base engine *e*, but delivers them in a different sequence. The state `x`<sub>i</sub> of a `shuffle_order_engine` engine adaptor object `x` consists of the state `e`<sub>i</sub> of its base engine `e`, an additional value *Y* of the type delivered by `e`, and an additional sequence *V* of *k* values also of the type delivered by `e`. The size of the state is the size of *e*’s state plus *k* + 1.

**2** The transition algorithm permutes the values produced by *e*. The state transition is performed as follows:

- Calculate an integer $j = \left\lfloor \frac{k \cdot (Y - e_{\min})} {e_{\max} - e_{\min} +1} \right\rfloor$.
- Set *Y* to *V*<sub>*j*</sub> and then set *V*<sub>*j*</sub> to `e()`.

**3** The generation algorithm yields the last value of `Y` produced while advancing `e`’s state as described above.

```cpp
template<class Engine, size_t k>
  class shuffle_order_engine {
  public:
    // types
    using result_type = typename Engine::result_type;

    // engine characteristics
    static constexpr size_t table_size = k;
    static constexpr result_type min() { return Engine::min(); }
    static constexpr result_type max() { return Engine::max(); }

    // constructors and seeding functions
    shuffle_order_engine();
    explicit shuffle_order_engine(const Engine& e);
    explicit shuffle_order_engine(Engine&& e);
    explicit shuffle_order_engine(result_type s);
    template<class Sseq> explicit shuffle_order_engine(Sseq& q);
    void seed();
    void seed(result_type s);
    template<class Sseq> void seed(Sseq& q);

    // generating functions
    result_type operator()();
    void discard(unsigned long long z);

    // property functions
    const Engine& base() const noexcept { return e; };

  private:
    Engine e;           // exposition only
    result_type V[k];   // exposition only
    result_type Y;      // exposition only
  };
```

**4** The following relation shall hold: `0 < k`.

**5** The textual representation consists of the textual representation of `e`, followed by the `k` values of *V*, followed by the value of *Y*.

**6** In addition to its behavior pursuant to subclause [[rand.req.adapt]](rand.req.adapt.md), each constructor that is not a copy constructor initializes `V[0]`, …, `V[k-1]` and *Y*, in that order, with values returned by successive invocations of `e()`.
