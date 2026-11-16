# 26.6.8.4.4 Class template `weibull_distribution` [rand.dist.pois.weibull]

[Contents](../README.md) · up: [26.6.8.4 Poisson distributions \[rand.dist.pois\]](rand.dist.pois.md) · prev: [26.6.8.4.3 Class template `gamma_distribution` \[rand.dist.pois.gamma\]](rand.dist.pois.gamma.md) · next: [26.6.8.4.5 Class template `extreme_value_distribution` \[rand.dist.pois.extreme\]](rand.dist.pois.extreme.md)

**1** A `weibull_distribution` random number distribution produces random numbers *x* ≥ 0 distributed according to the probability density function

$$p(x\,|\,a,b) = \frac{a}{b}
     \cdot \left(\frac{x}{b}\right)^{a-1}
     \cdot \, \exp\left( -\left(\frac{x}{b}\right)^a\right)
     \text{ .}$$

```cpp
template<class RealType = double>
  class weibull_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructor and reset functions
    weibull_distribution() : weibull_distribution(1.0) {}
    explicit weibull_distribution(RealType a, RealType b = 1.0);
    explicit weibull_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    RealType a() const;
    RealType b() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit weibull_distribution(RealType a, RealType b = 1.0);
```

**2** *Preconditions:* 0 &lt; `a` and 0 &lt; `b`.

**3** *Remarks:* `a` and `b` correspond to the respective parameters of the distribution.

```cpp
RealType a() const;
```

**4** *Returns:* The value of the `a` parameter with which the object was constructed.

```cpp
RealType b() const;
```

**5** *Returns:* The value of the `b` parameter with which the object was constructed.
