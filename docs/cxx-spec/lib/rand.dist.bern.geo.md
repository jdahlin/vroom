# 26.6.8.3.3 Class template `geometric_distribution` [rand.dist.bern.geo]

[Contents](../README.md) · up: [26.6.8.3 Bernoulli distributions \[rand.dist.bern\]](rand.dist.bern.md) · prev: [26.6.8.3.2 Class template `binomial_distribution` \[rand.dist.bern.bin\]](rand.dist.bern.bin.md) · next: [26.6.8.3.4 Class template `negative_binomial_distribution` \[rand.dist.bern.negbin\]](rand.dist.bern.negbin.md)

**1** A `geometric_distribution` random number distribution produces integer values *i* ≥ 0 distributed according to the discrete probability function

$$P(i\,|\,p) = p \cdot (1-p)^{i} \text{ .}$$

```cpp
template<class IntType = int>
  class geometric_distribution {
  public:
    // types
    using result_type = IntType;
    using param_type  = unspecified;

    // constructors and reset functions
    geometric_distribution() : geometric_distribution(0.5) {}
    explicit geometric_distribution(double p);
    explicit geometric_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    double p() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit geometric_distribution(double p);
```

**2** *Preconditions:* 0 &lt; `p` &lt; 1.

**3** *Remarks:* `p` corresponds to the parameter of the distribution.

```cpp
double p() const;
```

**4** *Returns:* The value of the `p` parameter with which the object was constructed.
