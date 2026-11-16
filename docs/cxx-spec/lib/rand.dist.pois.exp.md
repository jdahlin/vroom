# 26.6.8.4.2 Class template `exponential_distribution` [rand.dist.pois.exp]

[Contents](../README.md) · up: [26.6.8.4 Poisson distributions \[rand.dist.pois\]](rand.dist.pois.md) · prev: [26.6.8.4.1 Class template `poisson_distribution` \[rand.dist.pois.poisson\]](rand.dist.pois.poisson.md) · next: [26.6.8.4.3 Class template `gamma_distribution` \[rand.dist.pois.gamma\]](rand.dist.pois.gamma.md)

**1** An `exponential_distribution` random number distribution produces random numbers *x* > 0 distributed according to the probability density function

$$p(x\,|\,\lambda) = \lambda e^{-\lambda x} \text{ .}$$

```cpp
template<class RealType = double>
  class exponential_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructors and reset functions
    exponential_distribution() : exponential_distribution(1.0) {}
    explicit exponential_distribution(RealType lambda);
    explicit exponential_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    RealType lambda() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit exponential_distribution(RealType lambda);
```

**2** *Preconditions:* 0 &lt; `lambda`.

**3** *Remarks:* `lambda` corresponds to the parameter of the distribution.

```cpp
RealType lambda() const;
```

**4** *Returns:* The value of the `lambda` parameter with which the object was constructed.
