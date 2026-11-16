# 26.6.8.5.4 Class template `cauchy_distribution` [rand.dist.norm.cauchy]

[Contents](../README.md) · up: [26.6.8.5 Normal distributions \[rand.dist.norm\]](rand.dist.norm.md) · prev: [26.6.8.5.3 Class template `chi_squared_distribution` \[rand.dist.norm.chisq\]](rand.dist.norm.chisq.md) · next: [26.6.8.5.5 Class template `fisher_f_distribution` \[rand.dist.norm.f\]](rand.dist.norm.f.md)

**1** A `cauchy_distribution` random number distribution produces random numbers *x* distributed according to the probability density function

$$p(x\,|\,a,b) = \left(\pi b \left(1 + \left(\frac{x-a}{b} \right)^2 \, \right)\right)^{-1} \text{ .}$$

```cpp
template<class RealType = double>
  class cauchy_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructor and reset functions
    cauchy_distribution() : cauchy_distribution(0.0) {}
    explicit cauchy_distribution(RealType a, RealType b = 1.0);
    explicit cauchy_distribution(const param_type& parm);
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
explicit cauchy_distribution(RealType a, RealType b = 1.0);
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
