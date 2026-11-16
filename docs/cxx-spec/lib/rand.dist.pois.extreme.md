# 26.6.8.4.5 Class template `extreme_value_distribution` [rand.dist.pois.extreme]

[Contents](../README.md) · up: [26.6.8.4 Poisson distributions \[rand.dist.pois\]](rand.dist.pois.md) · prev: [26.6.8.4.4 Class template `weibull_distribution` \[rand.dist.pois.weibull\]](rand.dist.pois.weibull.md) · next: [26.6.8.5 Normal distributions \[rand.dist.norm\]](rand.dist.norm.md)

**1** An `extreme_value_distribution` random number distribution produces random numbers *x* distributed according to the probability density function[^1]

$$p(x\,|\,a,b) = \frac{1}{b}
     \cdot \exp\left(\frac{a-x}{b} - \exp\left(\frac{a-x}{b}\right)\right)
     \text{ .}$$

```cpp
template<class RealType = double>
  class extreme_value_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructor and reset functions
    extreme_value_distribution() : extreme_value_distribution(0.0) {}
    explicit extreme_value_distribution(RealType a, RealType b = 1.0);
    explicit extreme_value_distribution(const param_type& parm);
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
explicit extreme_value_distribution(RealType a, RealType b = 1.0);
```

**2** *Preconditions:* 0 &lt; `b`.

**3** *Remarks:* `a` and `b` correspond to the respective parameters of the distribution.

```cpp
RealType a() const;
```

**4** *Returns:* The value of the `a` parameter with which the object was constructed.

```cpp
RealType b() const;
```

**5** *Returns:* The value of the `b` parameter with which the object was constructed.

[^1]: The distribution corresponding to this probability density function is also known (with a possible change of variable) as the Gumbel Type I, the log-Weibull, or the Fisher-Tippett Type I distribution.
