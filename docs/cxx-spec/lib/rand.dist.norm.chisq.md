# 26.6.8.5.3 Class template `chi_squared_distribution` [rand.dist.norm.chisq]

[Contents](../README.md) · up: [26.6.8.5 Normal distributions \[rand.dist.norm\]](rand.dist.norm.md) · prev: [26.6.8.5.2 Class template `lognormal_distribution` \[rand.dist.norm.lognormal\]](rand.dist.norm.lognormal.md) · next: [26.6.8.5.4 Class template `cauchy_distribution` \[rand.dist.norm.cauchy\]](rand.dist.norm.cauchy.md)

**1** A `chi_squared_distribution` random number distribution produces random numbers *x* > 0 distributed according to the probability density function

$$p(x\,|\,n) = \frac{x^{(n/2)-1} \cdot e^{-x/2}}{\Gamma(n/2) \cdot 2^{n/2}} \text{ .}$$

```cpp
template<class RealType = double>
  class chi_squared_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructor and reset functions
    chi_squared_distribution() : chi_squared_distribution(1.0) {}
    explicit chi_squared_distribution(RealType n);
    explicit chi_squared_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    RealType n() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit chi_squared_distribution(RealType n);
```

**2** *Preconditions:* 0 &lt; `n`.

**3** *Remarks:* `n` corresponds to the parameter of the distribution.

```cpp
RealType n() const;
```

**4** *Returns:* The value of the `n` parameter with which the object was constructed.
