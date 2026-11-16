# 26.6.4.2 Class template `discard_block_engine` [rand.adapt.disc]

[Contents](../README.md) · up: [26.6.4 Random number engine adaptor class templates \[rand.adapt\]](rand.adapt.md) · prev: [26.6.4.1 In general \[rand.adapt.general\]](rand.adapt.general.md) · next: [26.6.4.3 Class template `independent_bits_engine` \[rand.adapt.ibits\]](rand.adapt.ibits.md)

**1** A `discard_block_engine` random number engine adaptor produces random numbers selected from those produced by some base engine *e*. The state `x`<sub>i</sub> of a `discard_block_engine` engine adaptor object `x` consists of the state `e`<sub>i</sub> of its base engine `e` and an additional integer *n*. The size of the state is the size of *e*’s state plus 1.

**2** The transition algorithm discards all but *r* > 0 values from each block of *p* ≥ *r* values delivered by *e*. The state transition is performed as follows: If *n* ≥ *r*, advance the state of `e` from `e`<sub>i</sub> to `e`<sub>i+p-r</sub> and set *n* to 0. In any case, then increment *n* and advance `e`’s then-current state `e`<sub>j</sub> to `e`<sub>j+1</sub>.

**3** The generation algorithm yields the value returned by the last invocation of `e()` while advancing `e`’s state as described above.

```cpp
template<class Engine, size_t p, size_t r>
  class discard_block_engine {
  public:
    // types
    using result_type = typename Engine::result_type;

    // engine characteristics
    static constexpr size_t block_size = p;
    static constexpr size_t used_block = r;
    static constexpr result_type min() { return Engine::min(); }
    static constexpr result_type max() { return Engine::max(); }

    // constructors and seeding functions
    discard_block_engine();
    explicit discard_block_engine(const Engine& e);
    explicit discard_block_engine(Engine&& e);
    explicit discard_block_engine(result_type s);
    template<class Sseq> explicit discard_block_engine(Sseq& q);
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
    int n;      // exposition only
  };
```

**4** The following relations shall hold: `0 < r` and `r <= p`.

**5** The textual representation consists of the textual representation of `e` followed by the value of `n`.

**6** In addition to its behavior pursuant to subclause [[rand.req.adapt]](rand.req.adapt.md), each constructor that is not a copy constructor sets `n` to 0.
