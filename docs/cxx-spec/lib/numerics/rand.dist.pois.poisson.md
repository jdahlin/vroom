# 26.6.8.4.1 Class template `poisson_distribution` [rand.dist.pois.poisson]

[Contents](../../README.md) · up: [26.6.8.4 Poisson distributions \[rand.dist.pois\]](rand.dist.pois.md) · prev: [26.6.8.4 Poisson distributions \[rand.dist.pois\]](rand.dist.pois.md) · next: [26.6.8.4.2 Class template `exponential_distribution` \[rand.dist.pois.exp\]](rand.dist.pois.exp.md)

**1** A `poisson_distribution` random number distribution produces integer values *i* ≥ 0 distributed according to the discrete probability function

$$P(i\,|\,\mu) = \frac{e^{-\mu} \mu^{i}}{i\,!} \text{ .}$$

The distribution parameter μ is also known as this distribution’s *mean*.

```cpp
template<class IntType = int>
  class poisson_distribution
  {
  public:
    // types
    using result_type = IntType;
    using param_type  = unspecified;

    // constructors and reset functions
    poisson_distribution() : poisson_distribution(1.0) {}
    explicit poisson_distribution(double mean);
    explicit poisson_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    double mean() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit poisson_distribution(double mean);
```

**2** *Preconditions:* 0 &lt; `mean`.

**3** *Remarks:* `mean` corresponds to the parameter of the distribution.

```cpp
double mean() const;
```

**4** *Returns:* The value of the `mean` parameter with which the object was constructed.
