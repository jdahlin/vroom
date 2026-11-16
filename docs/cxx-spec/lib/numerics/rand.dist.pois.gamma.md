# 26.6.8.4.3 Class template `gamma_distribution` [rand.dist.pois.gamma]

[Contents](../../README.md) · up: [26.6.8.4 Poisson distributions \[rand.dist.pois\]](rand.dist.pois.md) · prev: [26.6.8.4.2 Class template `exponential_distribution` \[rand.dist.pois.exp\]](rand.dist.pois.exp.md) · next: [26.6.8.4.4 Class template `weibull_distribution` \[rand.dist.pois.weibull\]](rand.dist.pois.weibull.md)

**1** A `gamma_distribution` random number distribution produces random numbers *x* > 0 distributed according to the probability density function

$$p(x\,|\,\alpha,\beta) =
     \frac{e^{-x/\beta}}{\beta^{\alpha} \cdot \Gamma(\alpha)} \, \cdot \, x^{\, \alpha-1}
     \text{ .}$$

```cpp
template<class RealType = double>
  class gamma_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructors and reset functions
    gamma_distribution() : gamma_distribution(1.0) {}
    explicit gamma_distribution(RealType alpha, RealType beta = 1.0);
    explicit gamma_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    RealType alpha() const;
    RealType beta() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit gamma_distribution(RealType alpha, RealType beta = 1.0);
```

**2** *Preconditions:* 0 &lt; `alpha` and 0 &lt; `beta`.

**3** *Remarks:* `alpha` and `beta` correspond to the parameters of the distribution.

```cpp
RealType alpha() const;
```

**4** *Returns:* The value of the `alpha` parameter with which the object was constructed.

```cpp
RealType beta() const;
```

**5** *Returns:* The value of the `beta` parameter with which the object was constructed.
